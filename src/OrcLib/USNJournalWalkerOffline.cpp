//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "USNJournalWalkerOffline.h"
#include "FileFind.h"
#include "MountedVolumeReader.h"

#include "MFTWalker.h"

#include <cmath>

#include <boost/scope_exit.hpp>

using namespace Orc;

static const auto ROOT_USN = 0x0005000000000005LL;

DWORD USNJournalWalkerOffline::m_BufferSize = 0x10000;

USNJournalWalkerOffline::USNJournalWalkerOffline()
    : m_location()
{
    m_dwlRootUSN = ROOT_USN;
    m_cchMaxComponentLength = 255;
    m_dwRecordMaxSize = (m_cchMaxComponentLength * sizeof(WCHAR) + sizeof(USN_RECORD) - sizeof(WCHAR)) + sizeof(DWORD);
}

USNJournalWalkerOffline::~USNJournalWalkerOffline() {}

const std::shared_ptr<ByteStream>& USNJournalWalkerOffline::GetUsnJournal() const
{
    return m_USNJournal;
}

void USNJournalWalkerOffline::SetUsnJournal(const std::shared_ptr<ByteStream>& usnJournal)
{
    m_USNJournal = usnJournal;
}

HRESULT USNJournalWalkerOffline::Initialize(const std::shared_ptr<Location>& loc)
{
    HRESULT hr = E_FAIL;

    m_VolReader = loc->GetReader();

    if (m_VolReader == nullptr)
    {
        return E_INVALIDARG;
    }

    FileFind fileFind(true);

    auto fs = std::make_shared<FileFind::SearchTerm>(L"$UsnJrnl:$J");
    fs->Required = FileFind::SearchTerm::NAME;

    fileFind.AddTerm(fs);

    m_location = loc;

    if (FAILED(
            hr = fileFind.Find(
                m_location,
                [this, hr](const std::shared_ptr<FileFind::Match>& aFileMatch, bool& bStop) {
                    Log::Info(
                        L"Found USN journal {}: {}",
                        aFileMatch->MatchingNames.front().FullPathName,
                        aFileMatch->Term->GetDescription());

                    if (aFileMatch->MatchingAttributes.size() > 0)
                    {
                        const FileFind::Match::AttributeMatch& attributeMatch = aFileMatch->MatchingAttributes.front();
                        m_USNJournal = attributeMatch.DataStream;
                        bStop = true;
                    }
                    else
                    {
                        Log::Error("Failed to find USN journal data attribute");
                    }
                },
                false,
                ResurrectRecordsMode::kNo)))
    {
        Log::Error("Failed to parse location while searching for USN journal");
    }

    if (FAILED(hr = m_RecordStore.InitializeStore(USN_MAX_NUMBER, m_dwRecordMaxSize)))
    {
        return hr;
    }

    return S_OK;
}

HRESULT USNJournalWalkerOffline::EnumJournal(const IUSNJournalWalker::Callbacks& pCallbacks)
{
    HRESULT hr = E_FAIL;

    if (m_location == nullptr)
        return hr;

    MFTWalker walk;

    if (FAILED(hr = walk.Initialize(m_location, ResurrectRecordsMode::kNo)))
    {
        Log::Error(L"Failed during MFT walk initialisation [{}]", SystemError(hr));
        return hr;
    }

    MFTWalker::Callbacks callbacks;

    callbacks.DirectoryCallback = [this](
                                      const std::shared_ptr<VolumeReader>& volreader,
                                      MFTRecord* pElt,
                                      const PFILE_NAME pFileName,
                                      const std::shared_ptr<IndexAllocationAttribute>& pAttr) {
        LPBYTE pUSNRecord = (LPBYTE)m_RecordStore.GetNewCell();

        if (pUSNRecord != nullptr)
        {
            LARGE_INTEGER* pLI = (LARGE_INTEGER*)&pElt->GetFileReferenceNumber();
            DWORDLONG frn = (DWORDLONG)pLI->QuadPart;

            // we don't add the root folder and the records with filenames that use the 8.3 format only
            if (frn != ROOT_USN && pFileName->Flags != FILE_NAME_DOS83)
            {
                WCHAR* fileName = (WCHAR*)&(pFileName->FileName);
                DWORD fileNameLength = (DWORD)pFileName->FileNameLength * sizeof(WCHAR);

                USN_RECORD record;
                FillUSNRecord(record, pElt, pFileName);

                memset(pUSNRecord, 0, m_dwRecordMaxSize);
                memcpy_s(pUSNRecord, m_dwRecordMaxSize, &record, record.RecordLength - fileNameLength);
                memcpy_s(
                    pUSNRecord + ((__int64)&record.FileName - (__int64)&record),
                    m_dwRecordMaxSize,
                    fileName,
                    fileNameLength);

                m_USNMap.insert(std::pair<DWORDLONG, USN_RECORD*>(record.FileReferenceNumber, (USN_RECORD*)pUSNRecord));
            }
            else
                m_RecordStore.FreeCell(pUSNRecord);
        }
    };

    if (FAILED(hr = walk.Walk(callbacks)))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
        {
            Log::Debug("Enumeration is stopping");
        }
        else
        {
            Log::Error("Failed during MFT walk [{}]", SystemError(hr));
        }
        return hr;
    }

    return S_OK;
}

HRESULT USNJournalWalkerOffline::ReadJournal(const IUSNJournalWalker::Callbacks& pCallbacks)
{
    HRESULT hr = E_FAIL;

    if (m_USNJournal)
    {
        ULONGLONG numBytesReturned = 0;
        LPVOID pChunk = HeapAlloc(GetProcessHeap(), 0, m_BufferSize);
        if (pChunk == nullptr)
            return E_OUTOFMEMORY;

        BOOST_SCOPE_EXIT(&pChunk)
        {
            HeapFree(GetProcessHeap(), 0, pChunk);
        }
        BOOST_SCOPE_EXIT_END;

        if (S_OK == m_USNJournal->CanRead())
        {
            ULONG64 size = m_USNJournal->GetSize();
            ULONG64 offset = 0;
            bool shouldStop = false;

            while (!shouldStop && offset < size)
            {
                if (FAILED(hr = m_USNJournal->SetFilePointer(offset, FILE_BEGIN, NULL)))
                    return hr;

                if (S_OK
                    != m_USNJournal->Read(pChunk, std::min<ULONGLONG>(m_BufferSize, size - offset), &numBytesReturned))
                {
                    return hr;
                }

                BYTE* pCurrentChunkPosition = reinterpret_cast<BYTE*>(pChunk);
                BYTE* pEndChunkPosition = reinterpret_cast<BYTE*>(pChunk) + numBytesReturned;
                USN_RECORD* nextUSNRecord = nullptr;
                ULONG64 adjustmentOffset = 0;
                bool shouldReadAnotherChunk = false;

                if (S_OK
                    != FindNextUSNRecord(
                        pCurrentChunkPosition,
                        pEndChunkPosition,
                        (BYTE**)&nextUSNRecord,
                        shouldReadAnotherChunk,
                        adjustmentOffset,
                        shouldStop))
                {
                    if (shouldStop)
                    {
                        break;
                    }
                }
                else
                {
                    // parse all the entries we just got
                    while (!shouldReadAnotherChunk && nextUSNRecord != nullptr)
                    {
                        pCurrentChunkPosition = reinterpret_cast<BYTE*>(nextUSNRecord);
                        bool bInSpecificLocation = false;
                        WCHAR* pFullName = GetFullNameAndIfInLocation(nextUSNRecord, NULL, &bInSpecificLocation);

                        if (pFullName && bInSpecificLocation)
                        {
                            pCallbacks.RecordCallback(m_VolReader, pFullName, nextUSNRecord);
                            m_dwWalkedItems++;
                        }

                        pCurrentChunkPosition += nextUSNRecord->RecordLength;

                        if (S_OK
                            != FindNextUSNRecord(
                                pCurrentChunkPosition,
                                pEndChunkPosition,
                                (BYTE**)&nextUSNRecord,
                                shouldReadAnotherChunk,
                                adjustmentOffset,
                                shouldStop))
                        {
                            if (shouldStop)
                            {
                                break;
                            }
                        }
                    }
                }

                offset += (numBytesReturned - adjustmentOffset);
            }
        }

        hr = S_OK;
    }

    return hr;
}

void USNJournalWalkerOffline::FillUSNRecord(USN_RECORD& record, MFTRecord* pElt, const PFILE_NAME pFileName)
{
    DWORD fileNameLength = (DWORD)pFileName->FileNameLength * sizeof(WCHAR);

    record.RecordLength = sizeof(USN_RECORD) + fileNameLength - (1 * sizeof(WCHAR));
    record.MajorVersion = 0;
    record.MinorVersion = 0;

    LARGE_INTEGER* pLI = (LARGE_INTEGER*)&pElt->GetFileReferenceNumber();
    record.FileReferenceNumber = (DWORDLONG)pLI->QuadPart;

    pLI = (LARGE_INTEGER*)&pFileName->ParentDirectory;
    record.ParentFileReferenceNumber = (DWORDLONG)pLI->QuadPart;

    record.Usn = pElt->GetStandardInformation()->USN;
    record.TimeStamp = {{0}};
    record.Reason = 0;
    record.SourceInfo = 0;
    record.SecurityId = pElt->GetStandardInformation()->SecurityId;
    record.FileAttributes = pElt->GetStandardInformation()->FileAttributes;
    record.FileNameOffset = (WORD)((__int64)&record.FileName - (__int64)&record);
    record.FileNameLength = (WORD)fileNameLength;
}

HRESULT USNJournalWalkerOffline::FindNextUSNRecord(
    BYTE* pCurrentChunkPosition,
    BYTE* pEndChunkPosition,
    BYTE** nextUSNRecord,
    bool& shouldReadAnotherChunk,
    ULONG64& adjustmentOffset,
    bool& shouldStop)
{
    *nextUSNRecord = nullptr;
    adjustmentOffset = 0;
    shouldReadAnotherChunk = false;
    shouldStop = false;

    // ensure we are not in a gap between 2 records
    ULONG offset = 0;
    if (*(DWORD*)(pCurrentChunkPosition) == 0)
    {
        while ((__int64)pCurrentChunkPosition + offset < (__int64)pEndChunkPosition
               && (*(pCurrentChunkPosition + offset) == 0))
        {
            ++offset;
        }
    }

    // I add 8 here in to ensure that we have at least 8 bytes to check that it's a valid record
    if ((__int64)pCurrentChunkPosition + offset + 8 >= (__int64)pEndChunkPosition)
    {
        shouldReadAnotherChunk = true;
        adjustmentOffset = (__int64)pEndChunkPosition - (__int64)(pCurrentChunkPosition + offset);
        return E_FAIL;
    }

    if (offset > 0)
    {
        // need to adjust offset to match the start of the USN_RECORD structure because of a gap
        for (int i = 4; i >= 1; i--)
        {
            if (*((WORD*)(pCurrentChunkPosition + offset + i)) == 0x2)  // TODO : handle version 3
            {
                offset -= (4 - i);
                break;
            }
        }
    }

    USN_RECORD* usnRecord = reinterpret_cast<USN_RECORD*>(pCurrentChunkPosition + offset);

    // check we have a right version number
    if (usnRecord->MajorVersion != 2)  // TODO : handle version 3
    {
        shouldStop = true;
        return E_FAIL;
    }

    __int64 usnRecordEndPosition = (__int64)(usnRecord) + usnRecord->RecordLength;

    if (usnRecordEndPosition > (__int64)pEndChunkPosition)
    {
        shouldReadAnotherChunk = true;
        adjustmentOffset = (__int64)pEndChunkPosition - (__int64)(usnRecord);
        return E_FAIL;
    }

    *nextUSNRecord = (BYTE*)usnRecord;

    return S_OK;
}

DWORD USNJournalWalkerOffline::GetBufferSize()
{
    return m_BufferSize;
}

void USNJournalWalkerOffline::SetBufferSize(DWORD size)
{
    m_BufferSize = size;
}
