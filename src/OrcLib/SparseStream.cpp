//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "SparseStream.h"

HRESULT Orc::SparseStream::OpenFile(
    __in PCWSTR pwzPath,
    __in DWORD dwDesiredAccess,
    __in DWORD dwSharedMode,
    __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
    __in DWORD dwCreationDisposition,
    __in DWORD dwFlagsAndAttributes,
    __in_opt HANDLE hTemplate)
{

    if (auto hr = Orc::FileStream::OpenFile(
            pwzPath,
            dwDesiredAccess,
            dwSharedMode,
            pSecurityAttributes,
            dwCreationDisposition,
            dwFlagsAndAttributes,
            hTemplate);
        FAILED(hr))
        return hr;

    if (!DeviceIoControl(m_hFile, FSCTL_SET_SPARSE, NULL, 0L, NULL, 0L, NULL, NULL))
    {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(L"Failed to set file '{}' sparse (code: {:#x})", pwzPath, hr);
        return hr;
    }

    return S_OK;
}

STDMETHODIMP Orc::SparseStream::SetSize(ULONG64 ullSize)
{
    if (!DeviceIoControl(m_hFile, FSCTL_SET_SPARSE, NULL, 0L, NULL, 0L, NULL, NULL))
    {
        auto hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(L"Failed to set file '{}' sparse (code: {:#x})", m_strPath, hr);
        return hr;
    }
    return Orc::FileStream::SetSize(ullSize);
}

STDMETHODIMP Orc::SparseStream::GetAllocatedRanges(std::vector<FILE_ALLOCATED_RANGE_BUFFER>& ranges)
{
    FILE_ALLOCATED_RANGE_BUFFER queryrange;  // Range to be examined

    queryrange.FileOffset.QuadPart = 0LLU;  // File range to query
    queryrange.Length.QuadPart = GetSize();  //   (the whole file)

    ranges.clear();

    while (TRUE)
    {
        bool bMoreData = false;
        FILE_ALLOCATED_RANGE_BUFFER range_buffer[512];  // Allocated areas info
        DWORD nbBytesReturned = 0LU;

        ZeroMemory(range_buffer, sizeof(range_buffer));

        if (!::DeviceIoControl(
                m_hFile,
                FSCTL_QUERY_ALLOCATED_RANGES,
                &queryrange,
                sizeof(queryrange),
                range_buffer,
                sizeof(range_buffer),
                &nbBytesReturned,
                NULL))
        {
            if (auto err = ::GetLastError(); err != ERROR_MORE_DATA)
            {
                spdlog::error(
                    L"Failed to read allocated ranges of sparse stream '{}' (code: {:#x})",
                    m_strPath,
                    HRESULT_FROM_WIN32(err));
                return HRESULT_FROM_WIN32(err);
            }
            bMoreData = true;
        }

        // Calculate the number of records returned and add them
        auto nbRanges = nbBytesReturned / sizeof(FILE_ALLOCATED_RANGE_BUFFER);
        ranges.reserve(ranges.size() + nbRanges);
        for (auto i = 0; i < nbRanges; i++)
            ranges.push_back(range_buffer[i]);

        if (!ranges.empty())
        {
            queryrange.FileOffset.QuadPart = ranges.back().FileOffset.QuadPart + ranges.back().Length.QuadPart;
            queryrange.Length.QuadPart = GetSize() - queryrange.FileOffset.QuadPart;
        }

        if (!bMoreData)
            break;
    }
    return S_OK;
}

Orc::SparseStream::~SparseStream() {}
