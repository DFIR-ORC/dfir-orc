//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "FileMappingStream.h"

using namespace Orc;

STDMETHODIMP
FileMappingStream::Open(_In_ HANDLE hFile, _In_ DWORD flProtect, _In_ ULONGLONG ullMaximumSize, _In_opt_ LPWSTR lpName)
{
    HRESULT hr = E_FAIL;
    LARGE_INTEGER liMaxSize;
    liMaxSize.QuadPart = ullMaximumSize;

    m_dwProtect = flProtect;

    switch (m_dwProtect)
    {
        case PAGE_READONLY:
        case PAGE_READWRITE:
            break;
        default:
            Log::Error("The only allowed protection for FileMappingStream are PAGE_READONLY _or_ PAGE_READWRITE");
            return E_INVALIDARG;
    }

    m_hMapping = CreateFileMapping(hFile, NULL, flProtect | SEC_RESERVE, liMaxSize.HighPart, liMaxSize.LowPart, lpName);
    if (m_hMapping == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CreateFileMapping (hFile: 0x{:p}, code: {:#x})", hFile, hr);
        return hr;
    }

    switch (m_dwProtect)
    {
        case PAGE_READONLY:
            m_dwViewProtect = FILE_MAP_READ;
            m_dwPageProtect = PAGE_READONLY;
            break;
        case PAGE_READWRITE:
            m_dwViewProtect = FILE_MAP_ALL_ACCESS;
            m_dwPageProtect = PAGE_READWRITE;
            break;
        default:
            break;
    }

    m_pMapped = (PBYTE)MapViewOfFile(m_hMapping, m_dwViewProtect, 0L, 0L, static_cast<size_t>(ullMaximumSize));
    if (m_pMapped == NULL)
    {
        CloseHandle(m_hMapping);
        m_hMapping = INVALID_HANDLE_VALUE;

        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed MapViewOfFile (hFile: 0x{:p}, code: {:#x})", hFile, hr);
        return hr;
    }

    return S_OK;
}

HRESULT FileMappingStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    ULONGLONG ullBytesToRead = cbBytes;

    if (cbBytes + m_ullCurrentPosition > m_ullDataSize)
        ullBytesToRead = _abs64(((LONGLONG)m_ullDataSize) - m_ullCurrentPosition);

    CopyMemory(pReadBuffer, m_pMapped + m_ullCurrentPosition, static_cast<size_t>(ullBytesToRead));

    m_ullCurrentPosition += ullBytesToRead;

    if (pcbBytesRead != NULL)
        *pcbBytesRead = ullBytesToRead;

    return S_OK;
}

HRESULT FileMappingStream::CommitSize(ULONGLONG ullNewSize)
{
    HRESULT hr = E_FAIL;
    if (ullNewSize < m_ullCommittedSize)
        return S_OK;

    if (NULL == VirtualAlloc(m_pMapped, static_cast<size_t>(ullNewSize), MEM_COMMIT, m_dwPageProtect))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed VirtualAlloc: cannot Commit size {} (code: {:#x})", ullNewSize, hr);
        return hr;
    }

    MEMORY_BASIC_INFORMATION mbi;
    ZeroMemory(&mbi, sizeof(MEMORY_BASIC_INFORMATION));

    if (!VirtualQuery(m_pMapped, &mbi, sizeof(MEMORY_BASIC_INFORMATION)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed VirtualQuery on 0x{:p} (code: {:#x})", m_pMapped, hr);

        if (!VirtualFree(m_pMapped, static_cast<size_t>(ullNewSize), MEM_DECOMMIT))
        {
            Log::Error("Failed VirtualFree: cannot decommit 0x{:p} (code: {:#x})", m_pMapped, LastWin32Error());
        }

        return hr;
    }
    m_ullCommittedSize = mbi.RegionSize;
    return S_OK;
}

HRESULT FileMappingStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);
    HRESULT hr = E_FAIL;

    if (pcbBytesWritten)
        *pcbBytesWritten = 0LL;

    if (m_ullCurrentPosition + cbBytesToWrite > m_ullDataSize)
    {
        if (FAILED(hr = CommitSize(m_ullCurrentPosition + cbBytesToWrite)))
            return hr;
    }

    CopyMemory(m_pMapped + m_ullCurrentPosition, pWriteBuffer, static_cast<size_t>(cbBytesToWrite));
    m_ullCurrentPosition += cbBytesToWrite;
    m_ullDataSize += cbBytesToWrite;

    if (pcbBytesWritten)
        *pcbBytesWritten = cbBytesToWrite;
    return S_OK;
}

HRESULT FileMappingStream::SetFilePointer(
    __in LONGLONG DistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pCurrPointer)
{
    HRESULT hr = E_FAIL;

    switch (dwMoveMethod)
    {
        case FILE_BEGIN:
            if (DistanceToMove < 0LL)
                return E_INVALIDARG;
            if ((ULONGLONG)DistanceToMove > m_ullCommittedSize)
            {
                if (FAILED(hr = CommitSize(DistanceToMove)))
                    return hr;
            }
            m_ullCurrentPosition = DistanceToMove;
            if ((ULONGLONG)DistanceToMove > m_ullDataSize)
                m_ullDataSize = DistanceToMove;
            break;
        case FILE_CURRENT:
            if ((m_ullCurrentPosition + DistanceToMove) > m_ullCommittedSize)
            {
                if (FAILED(hr = CommitSize(m_ullCurrentPosition + DistanceToMove)))
                    return hr;
            }
            m_ullCurrentPosition += DistanceToMove;
            if ((m_ullCurrentPosition + DistanceToMove) > m_ullDataSize)
                m_ullDataSize = m_ullCurrentPosition + DistanceToMove;
            break;
        case FILE_END:
            if ((m_ullDataSize + DistanceToMove) > m_ullCommittedSize)
            {
                if (FAILED(hr = CommitSize(m_ullDataSize + DistanceToMove)))
                    return hr;
            }
            m_ullCurrentPosition = m_ullDataSize + DistanceToMove;
            if ((m_ullDataSize + DistanceToMove) > m_ullDataSize)
                m_ullDataSize += DistanceToMove;
            break;
    }
    if (pCurrPointer != NULL)
        *pCurrPointer = m_ullCurrentPosition;
    return S_OK;
}

ULONG64 FileMappingStream::GetSize()
{
    return m_ullDataSize;
}

STDMETHODIMP FileMappingStream::SetSize(ULONG64 NewSize)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = CommitSize(NewSize)))
        return hr;

    m_ullDataSize = NewSize;
    if (m_ullCurrentPosition > m_ullDataSize)
        m_ullCurrentPosition = m_ullDataSize;
    return S_OK;
}

HRESULT FileMappingStream::Close()
{
    if (m_pMapped != NULL)
    {
        UnmapViewOfFile(m_pMapped);
        m_pMapped = NULL;
    }
    if (m_hMapping != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hMapping);
        m_hMapping = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}

FileMappingStream::~FileMappingStream(void)
{
    Close();
}
