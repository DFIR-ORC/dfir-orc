//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "USNJournalWalkerOffline.h"
#include "FileFind.h"
#include "MountedVolumeReader.h"

#include "MFTWalker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace Orc;

static const auto ROOT_USN = 0x0005000000000005LL;

namespace {

// NTFS writes a USN_RECORD_V2 as its fixed part, then the name, padded to 8 bytes. A name has at most 255 characters.
constexpr DWORD kUsnRecordV2FixedSize = offsetof(USN_RECORD, FileName);
constexpr DWORD kUsnRecordV2MaxNameBytes = 255 * sizeof(WCHAR);
constexpr DWORD kUsnRecordV2MaxSize = (kUsnRecordV2FixedSize + kUsnRecordV2MaxNameBytes + 7) & ~DWORD(7);

// The granularity at which a failed read is retried: what still fails is lost, and no more.
constexpr ULONG64 kSectorSize = 512;

ULONG64 CountNonZero(const BYTE* begin, const BYTE* end)
{
    return static_cast<ULONG64>(std::count_if(begin, end, [](BYTE b) { return b != 0; }));
}

// True when the fixed part at 'record' describes a version 2.0 record whose lengths agree with each other. Every field
// comes from the journal and may be garbage: a header is only trusted when the record it describes is the one NTFS
// would have written, so that a false-small RecordLength cannot hold the walk in place and a stray 02 00 in corrupted
// data is not taken for a record. Version 3 and 4 records are not read: like damaged data, they are passed over and
// counted as unrecognized, and the walk goes on to the version 2 records after them.
bool IsConsistentUsnRecordV2(const USN_RECORD& record)
{
    if (record.MajorVersion != 2 || record.MinorVersion != 0)
    {
        return false;
    }

    if (record.FileNameOffset != kUsnRecordV2FixedSize || record.FileNameLength == 0
        || record.FileNameLength > kUsnRecordV2MaxNameBytes || (record.FileNameLength % sizeof(WCHAR)) != 0)
    {
        return false;
    }

    return record.RecordLength == ((kUsnRecordV2FixedSize + record.FileNameLength + 7) & ~DWORD(7));
}

}  // namespace

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
        Log::Error("Failed to parse location while searching for USN journal [{}]", SystemError(hr));
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
    if (!m_USNJournal)
    {
        return E_FAIL;
    }

    if (const HRESULT canRead = m_USNJournal->CanRead(); canRead != S_OK)
    {
        const HRESULT hr = FAILED(canRead) ? canRead : HRESULT_FROM_WIN32(ERROR_READ_FAULT);
        Log::Error(L"USN journal stream cannot be read [{}]", SystemError(hr));
        return hr;
    }

    // A chunk must hold the largest record, or a record starting a chunk could never be read whole.
    const DWORD chunkSize = std::max<DWORD>(m_BufferSize, kUsnRecordV2MaxSize);
    std::vector<BYTE> chunk(chunkSize);

    const ULONG64 size = m_USNJournal->GetSize();
    ULONG64 offset = 0;

    // Non-zero bytes passed over without a record: corrupted stretches, the slack of a reused cluster, the part of a
    // record cut by an unreadable sector.
    ULONG64 unrecognizedBytes = 0;

    // Non-zero bytes left at the end of the journal, too short to hold the record they start.
    ULONG64 truncatedTailBytes = 0;

    // Bytes that could not be read at all, and the first error that refused them.
    ULONG64 unreadableBytes = 0;
    HRESULT firstReadError = S_OK;

    while (offset < size)
    {
        const ULONG64 toRead = std::min<ULONG64>(chunkSize, size - offset);
        ULONG64 bytesRead = 0;
        std::vector<ByteRange> badRanges;

        if (const HRESULT hr = ReadChunk(offset, chunk.data(), toRead, bytesRead, badRanges); hr != S_OK)
        {
            if (firstReadError == S_OK)
            {
                firstReadError = hr;
            }
        }

        BYTE* const pChunk = chunk.data();
        BYTE* const pEndChunkPosition = pChunk + bytesRead;

        // Records are parsed between unreadable ranges, never across one: what a range cuts is given up with it, and
        // the walk resynchronises after it.
        ULONG64 segmentStart = 0;
        for (const auto& [badStart, badEnd] : badRanges)
        {
            const ULONG64 unchecked =
                ParseRecords(pChunk + segmentStart, pChunk + badStart, pCallbacks, unrecognizedBytes);
            unrecognizedBytes += CountNonZero(pChunk + badStart - unchecked, pChunk + badStart);
            unreadableBytes += badEnd - badStart;
            segmentStart = badEnd;
        }

        const ULONG64 adjustmentOffset =
            ParseRecords(pChunk + segmentStart, pEndChunkPosition, pCallbacks, unrecognizedBytes);

        // What was left unchecked at the end of the chunk is read again at the start of the next one. After an
        // unreadable range, that never goes back before the range.
        const ULONG64 advance = bytesRead - adjustmentOffset;
        if (advance != 0)
        {
            offset += advance;
            continue;
        }

        // Nothing in this chunk could be checked: it is shorter than a record header, or than the record it starts.
        // A chunk holds the largest record, so only the end of the journal, or a short read, leaves one that small.
        // Either way its bytes are given up and the walk goes on after them.
        const ULONG64 nonZero = CountNonZero(pChunk, pEndChunkPosition);
        if (bytesRead == 0 || offset + bytesRead >= size)
        {
            truncatedTailBytes = nonZero;
            break;
        }

        unrecognizedBytes += nonZero;
        offset += bytesRead;
    }

    if (size != 0 && unreadableBytes >= size)
    {
        Log::Error(L"USN journal could not be read at all ({} bytes) [{}]", size, SystemError(firstReadError));
        return firstReadError;
    }

    // Best effort: a damaged or unreadable stretch costs its own bytes, never the records around it. The loss is
    // reported, and the records that were read are the result.
    if (unrecognizedBytes != 0 || truncatedTailBytes != 0 || unreadableBytes != 0)
    {
        Log::Warn(
            L"USN journal is damaged: {} bytes could not be read{}, {} bytes hold no valid record and {} bytes end it "
            L"with an incomplete record; they were skipped and every valid record around them was read",
            unreadableBytes,
            unreadableBytes != 0 ? fmt::format(L" [{}]", SystemError(firstReadError)) : std::wstring(),
            unrecognizedBytes,
            truncatedTailBytes);
    }

    return S_OK;
}

HRESULT USNJournalWalkerOffline::ReadChunk(
    ULONG64 offset,
    BYTE* buffer,
    ULONG64 length,
    ULONG64& bytesRead,
    std::vector<ByteRange>& badRanges)
{
    bytesRead = 0;
    badRanges.clear();

    HRESULT hr = m_USNJournal->SetFilePointer(offset, FILE_BEGIN, NULL);
    if (SUCCEEDED(hr))
    {
        hr = m_USNJournal->Read(buffer, length, &bytesRead);
        if (hr == S_OK)
        {
            return S_OK;
        }
    }

    // The chunk is read again one sector at a time, so that only the sectors that still fail are lost. The error
    // returned is the one that refused the chunk.
    const HRESULT chunkError = FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    Log::Debug(
        L"Failed to read USN journal chunk at offset {:#x} ({} bytes); retrying by sector [{}]",
        offset,
        length,
        SystemError(chunkError));

    for (ULONG64 sector = 0; sector < length; sector += kSectorSize)
    {
        const ULONG64 sectorLength = std::min<ULONG64>(kSectorSize, length - sector);
        ULONG64 sectorRead = 0;

        HRESULT sectorHr = m_USNJournal->SetFilePointer(offset + sector, FILE_BEGIN, NULL);
        if (SUCCEEDED(sectorHr))
        {
            sectorHr = m_USNJournal->Read(buffer + sector, sectorLength, &sectorRead);
        }

        if (sectorHr == S_OK && sectorRead == sectorLength)
        {
            continue;
        }

        if (!badRanges.empty() && badRanges.back().second == sector)
        {
            badRanges.back().second = sector + sectorLength;
        }
        else
        {
            badRanges.emplace_back(sector, sector + sectorLength);
        }
    }

    Log::Debug(L"USN journal chunk at offset {:#x}: {} unreadable ranges", offset, badRanges.size());
    bytesRead = length;
    return chunkError;
}

ULONG64 USNJournalWalkerOffline::ParseRecords(
    BYTE* begin,
    BYTE* end,
    const IUSNJournalWalker::Callbacks& pCallbacks,
    ULONG64& unrecognizedBytes)
{
    BYTE* current = begin;
    for (;;)
    {
        USN_RECORD* nextUSNRecord = nullptr;
        bool shouldReadAnotherChunk = false;
        ULONG64 adjustmentOffset = 0;
        ULONG64 skippedBytes = 0;

        const HRESULT found = FindNextUSNRecord(
            current, end, (BYTE**)&nextUSNRecord, shouldReadAnotherChunk, adjustmentOffset, skippedBytes);

        unrecognizedBytes += skippedBytes;
        if (found != S_OK)
        {
            return adjustmentOffset;
        }

        bool bInSpecificLocation = false;
        WCHAR* pFullName = GetFullNameAndIfInLocation(nextUSNRecord, NULL, &bInSpecificLocation);

        if (pFullName && bInSpecificLocation)
        {
            pCallbacks.RecordCallback(m_VolReader, pFullName, nextUSNRecord);
            m_dwWalkedItems++;
        }

        current = reinterpret_cast<BYTE*>(nextUSNRecord) + nextUSNRecord->RecordLength;
    }
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
    ULONG64& unrecognizedBytes)
{
    *nextUSNRecord = nullptr;
    adjustmentOffset = 0;
    shouldReadAnotherChunk = false;
    unrecognizedBytes = 0;

    // A record may start at any byte once the journal is damaged, so every position is a candidate: zero padding
    // between records, the slack of a reused cluster or a torn write are passed over one byte at a time until a
    // header whose fields agree. Nothing is read before the fixed part is proven inside the chunk.
    BYTE* candidate = (std::min)(pCurrentChunkPosition, pEndChunkPosition);
    for (;; ++candidate)
    {
        const ULONG64 remaining = static_cast<ULONG64>(pEndChunkPosition - candidate);
        if (remaining < kUsnRecordV2FixedSize)
        {
            shouldReadAnotherChunk = true;
            adjustmentOffset = remaining;
            return E_FAIL;
        }

        const auto* usnRecord = reinterpret_cast<const USN_RECORD*>(candidate);
        if (IsConsistentUsnRecordV2(*usnRecord))
        {
            if (usnRecord->RecordLength > remaining)
            {
                shouldReadAnotherChunk = true;
                adjustmentOffset = remaining;
                return E_FAIL;
            }

            *nextUSNRecord = candidate;
            return S_OK;
        }

        if (*candidate != 0)
        {
            ++unrecognizedBytes;
        }
    }
}

DWORD USNJournalWalkerOffline::GetBufferSize()
{
    return m_BufferSize;
}

void USNJournalWalkerOffline::SetBufferSize(DWORD size)
{
    m_BufferSize = size;
}
