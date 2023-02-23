//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include <Winternl.h>
#include <WinIoCtl.h>

#include <map>
#include <vector>

#include "USNJournalWalkerBase.h"
#include "USNJournalWalker.h"

#include "MountedVolumeReader.h"

#include <boost/scope_exit.hpp>

using namespace std;
using namespace Orc;

// structs
struct ENUM_USN_DATA_OUTPUT_DATA
{
    USN usn;
    USN_RECORD usnRecord[ANYSIZE_ARRAY];
};

// structs
struct READ_USN_DATA_OUTPUT_DATA
{
    USN usn;
    USN_RECORD usnRecord[ANYSIZE_ARRAY];
};

// Buffer size for DeviceIOControl
constexpr auto USN_BUFFER_SIZE = (0x10000);

USNJournalWalker::USNJournalWalker(void)
{
    m_dwVolumeSerialNumber = 0;
}

HRESULT USNJournalWalker::Initialize(const std::shared_ptr<Location>& loc)
{
    HRESULT hr = E_FAIL;

    if (loc->GetType() != Location::Type::MountedVolume)
    {
        return E_INVALIDARG;
    }

    // open a handle to the root of the drive.
    m_VolReader = loc->GetReader();

    if (m_VolReader == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<MountedVolumeReader> mountedVolReader = dynamic_pointer_cast<MountedVolumeReader>(m_VolReader);

    if (mountedVolReader == nullptr)
    {
        return E_INVALIDARG;
    }

    mountedVolReader->LoadDiskProperties();

    if (mountedVolReader->GetDevice() == INVALID_HANDLE_VALUE)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // open a handle to the root of the drive.
    m_cchMaxComponentLength = 0;

    WCHAR szFSName[ORC_MAX_PATH];
    WCHAR szVolName[ORC_MAX_PATH];

    wcscpy_s(szVolName, mountedVolReader->ShortVolumeName());
    wcscat_s(szVolName, L"\\");

    if (GetVolumeInformation(szVolName, NULL, NULL, NULL, &m_cchMaxComponentLength, NULL, szFSName, ORC_MAX_PATH))
    {
        m_dwRecordMaxSize = (m_cchMaxComponentLength * sizeof(WCHAR) + sizeof(USN_RECORD) - sizeof(WCHAR))
            + sizeof(DWORD);  // We are adding 4 bytes because of testing results contradicting
                              // https://msdn.microsoft.com/en-us/library/windows/desktop/aa365722(v=vs.85).aspx
    }
    else
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (wcscmp(szFSName, L"NTFS"))
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_SYSTEM_LIMITATION);
    }

    HANDLE hRoot = CreateFile(
        szVolName,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    BY_HANDLE_FILE_INFORMATION fiRootFile;
    if (!GetFileInformationByHandle(hRoot, &fiRootFile))
    {
        CloseHandle(hRoot);
        hRoot = INVALID_HANDLE_VALUE;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    else
    {
        m_dwlRootUSN = (((DWORDLONG)fiRootFile.nFileIndexHigh) << 32) | fiRootFile.nFileIndexLow;
        m_dwVolumeSerialNumber = fiRootFile.dwVolumeSerialNumber;
    }
    CloseHandle(hRoot);

    auto& subdirs = loc->GetSubDirs();

    if (!subdirs.empty())
    {
        std::for_each(begin(subdirs), end(subdirs), [this](const std::wstring& location) {
            WCHAR* szExpandedLocation = NULL;
            DWORD dwLen = ExpandEnvironmentStrings(location.c_str(), NULL, 0L);
            szExpandedLocation = (WCHAR*)malloc((dwLen + 1) * sizeof(WCHAR));
            if (szExpandedLocation == NULL)
                return;

            BOOST_SCOPE_EXIT(&szExpandedLocation) { free(szExpandedLocation); }
            BOOST_SCOPE_EXIT_END;

            dwLen = ExpandEnvironmentStrings(location.c_str(), szExpandedLocation, dwLen + 1);
            if (!dwLen)
                return;

            HANDLE hLocation = CreateFile(
                szExpandedLocation,
                FILE_READ_EA | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                NULL);

            if (hLocation != INVALID_HANDLE_VALUE)
            {
                BY_HANDLE_FILE_INFORMATION fiLocation;
                if (GetFileInformationByHandle(hLocation, &fiLocation))
                {
                    if (m_dwVolumeSerialNumber == fiLocation.dwVolumeSerialNumber)
                        m_LocationsRefNum.insert(
                            (((DWORDLONG)fiLocation.nFileIndexHigh) << 32) | fiLocation.nFileIndexLow);
                }
                CloseHandle(hLocation);
            }
        });
    }

    if (FAILED(hr = m_RecordStore.InitializeStore(USN_MAX_NUMBER, m_dwRecordMaxSize)))
    {
        return hr;
    }
    return S_OK;
}

HRESULT USNJournalWalker::WalkRecords(const Callbacks& pCallbacks, bool bWalkDirs)
{
    if (pCallbacks.RecordCallback != NULL)
    {
        // Walk through the files
        auto iter = m_USNMap.begin();
        do
        {
            USN_RECORD* pValue = iter->second;

            if (pValue->FileAttributes & FILE_ATTRIBUTE_DIRECTORY && !bWalkDirs)
            {
                ++iter;
                continue;
            }

            bool bInSpecificLocation = false;
            WCHAR* pFullName = GetFullNameAndIfInLocation(pValue, NULL, &bInSpecificLocation);

            if (pFullName && bInSpecificLocation)
            {
                pCallbacks.RecordCallback(m_VolReader, pFullName, pValue);
                m_dwWalkedItems++;
            }

            if (!(pValue->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                m_USNMap.erase(iter++);
                m_RecordStore.FreeCell(pValue);
            }
            else
            {
                ++iter;
            }
        } while (iter != m_USNMap.end());
    }
    return S_OK;
}

HRESULT USNJournalWalker::EnumJournal(const IUSNJournalWalker::Callbacks& pCallbacks)
{
    MFT_ENUM_DATA InBuffer = {0, 0, MAXLONGLONG};
    DWORD numBytesReturned = 0;

    ENUM_USN_DATA_OUTPUT_DATA* pOutBuffer = (ENUM_USN_DATA_OUTPUT_DATA*)HeapAlloc(GetProcessHeap(), 0, USN_BUFFER_SIZE);
    if (pOutBuffer == nullptr)
        return E_OUTOFMEMORY;

    BOOST_SCOPE_EXIT(&pOutBuffer) { HeapFree(GetProcessHeap(), 0, pOutBuffer); }
    BOOST_SCOPE_EXIT_END;

    std::shared_ptr<MountedVolumeReader> mountedVolReader = dynamic_pointer_cast<MountedVolumeReader>(m_VolReader);

    if (mountedVolReader == nullptr)
        return E_INVALIDARG;

    bool bDone = false;
    while (bDone == false)
    {
        // walk the USN Journal
        if (!DeviceIoControl(
                mountedVolReader->GetDevice(),
                FSCTL_ENUM_USN_DATA,
                &InBuffer,
                sizeof(InBuffer),
                pOutBuffer,
                USN_BUFFER_SIZE,
                &numBytesReturned,
                NULL))
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_HANDLE_EOF)
                bDone = true;
            else
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        else
        {
            USN_RECORD* nextUSNRecord = pOutBuffer->usnRecord;

            // parse all of the entries we just got
            while ((__int64)nextUSNRecord < (__int64)((PUCHAR)pOutBuffer + numBytesReturned))
            {
                LPBYTE pElt = (LPBYTE)m_RecordStore.GetNewCell();

                if (pElt == NULL)
                {
                    // We walk through FILES for our alerady recorded nodes with hope this will free some space
                    WalkRecords(pCallbacks, false);
                    pElt = (LPBYTE)m_RecordStore.GetNewCell();
                    if (pElt == NULL)
                    {
                        /// We are still unable to move forward...
                        return E_OUTOFMEMORY;
                    }
                }

                memset(pElt, 0, m_dwRecordMaxSize);
                memcpy_s(pElt, m_dwRecordMaxSize, nextUSNRecord, nextUSNRecord->RecordLength);

                _ASSERT(m_USNMap.find(nextUSNRecord->FileReferenceNumber) == m_USNMap.end());
                m_USNMap.insert(
                    std::pair<DWORDLONG, USN_RECORD*>(nextUSNRecord->FileReferenceNumber, (USN_RECORD*)pElt));

                nextUSNRecord = (USN_RECORD*)((BYTE*)nextUSNRecord + nextUSNRecord->RecordLength);
            }
            InBuffer.StartFileReferenceNumber = pOutBuffer->usn;
        }
    }

    return WalkRecords(pCallbacks, true);
}

HRESULT USNJournalWalker::ReadJournal(const IUSNJournalWalker::Callbacks& pCallbacks)
{
    HRESULT hr = E_FAIL;
    USN_JOURNAL_DATA JournalData = {0};
    DWORD dwBytes = 0LU;

    std::shared_ptr<MountedVolumeReader> mountedVolReader = dynamic_pointer_cast<MountedVolumeReader>(m_VolReader);

    if (mountedVolReader == nullptr)
    {
        return E_INVALIDARG;
    }

    if (!DeviceIoControl(
            mountedVolReader->GetDevice(),
            FSCTL_QUERY_USN_JOURNAL,
            NULL,
            0,
            &JournalData,
            sizeof(JournalData),
            &dwBytes,
            NULL))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    READ_USN_JOURNAL_DATA InBuffer = {0, 0xFFFFFFFF, FALSE, 0, 0, JournalData.UsnJournalID};
    DWORD numBytesReturned = 0;
    READ_USN_DATA_OUTPUT_DATA* pOutBuffer = (READ_USN_DATA_OUTPUT_DATA*)HeapAlloc(GetProcessHeap(), 0, USN_BUFFER_SIZE);
    if (pOutBuffer == nullptr)
        return E_OUTOFMEMORY;

    BOOST_SCOPE_EXIT(&pOutBuffer) { HeapFree(GetProcessHeap(), 0, pOutBuffer); }
    BOOST_SCOPE_EXIT_END

    bool bDone = false;
    while (bDone == false)
    {
        // walk the USN Journal
        if (!DeviceIoControl(
                mountedVolReader->GetDevice(),
                FSCTL_READ_USN_JOURNAL,
                &InBuffer,
                sizeof(InBuffer),
                pOutBuffer,
                USN_BUFFER_SIZE,
                &numBytesReturned,
                NULL))
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_HANDLE_EOF)
                bDone = true;
            else
                return HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            if (numBytesReturned <= 8)
                bDone = true;
            else
            {
                USN_RECORD* nextUSNRecord = pOutBuffer->usnRecord;

                // parse all of the entries we just got
                while ((__int64)nextUSNRecord < (__int64)((PUCHAR)pOutBuffer + numBytesReturned))
                {

                    // Parsing entries...
                    bool bInSpecificLocation = false;
                    WCHAR* pFullName = GetFullNameAndIfInLocation(nextUSNRecord, NULL, &bInSpecificLocation);

                    if (pFullName && bInSpecificLocation)
                    {
                        pCallbacks.RecordCallback(m_VolReader, pFullName, nextUSNRecord);

                        if (!(nextUSNRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            m_USNMap.erase(nextUSNRecord->FileReferenceNumber);
                        }
                        m_dwWalkedItems++;
                    }

                    nextUSNRecord = (USN_RECORD*)((BYTE*)nextUSNRecord + nextUSNRecord->RecordLength);
                }

                InBuffer.StartUsn = pOutBuffer->usn;
            }
        }
    }
    return S_OK;
}

USNJournalWalker::~USNJournalWalker(void) {}
