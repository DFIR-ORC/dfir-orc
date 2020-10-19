//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "PipeStream.h"

using namespace Orc;

HRESULT PipeStream::CreatePipe(__in DWORD nSize)
{
    HRESULT hr = E_FAIL;

    if (m_hReadPipe != INVALID_HANDLE_VALUE || m_hWritePipe != INVALID_HANDLE_VALUE)
        Close();

    HANDLE hReadPipe, hWritePipe = INVALID_HANDLE_VALUE;

    if (!::CreatePipe(&hReadPipe, &hWritePipe, NULL, nSize))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CreatePipe: cannot create anonymous pipe (code: {:#x})", hr);
        return hr;
    }
    {
        ScopedLock sl(m_cs);
        std::swap(m_hReadPipe, hReadPipe);
        std::swap(m_hWritePipe, hWritePipe);
    }

    return S_OK;
}

__data_entrypoint(File) HRESULT PipeStream::Read(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (m_hReadPipe == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    *pcbBytesRead = 0;

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;

    DWORD dwBytesRead = 0;
    if (!ReadFile(m_hReadPipe, pBuffer, (DWORD)cbBytesToRead, &dwBytesRead, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());

        if (hr == HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE))
        {
            Log::Debug("Broken pipe");
        }
        else
        {
            Log::Error("ReadFile failed");
            return hr;
        }
    }
    Log::Debug("ReadFile read {} bytes (hFile={:p})", dwBytesRead, m_hReadPipe);
    *pcbBytesRead = dwBytesRead;
    return S_OK;
}

HRESULT
PipeStream::Write(__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (m_hWritePipe == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    DWORD cbBytesWritten = 0;
    if (cbBytes > MAXDWORD)
    {
        Log::Error("PipeStream: Write: Too many bytes");
        return E_INVALIDARG;
    }

    if (!WriteFile(m_hWritePipe, pBuffer, static_cast<DWORD>(cbBytes), &cbBytesWritten, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed WriteFile (code: {:#x})", hr);
        return hr;
    }
    Log::Debug("WriteFile {} bytes succeeded (hFile=0x{:p})", cbBytesWritten, m_hWritePipe);
    *pcbBytesWritten = cbBytesWritten;
    return S_OK;
}

HRESULT PipeStream::SetFilePointer(__in LONGLONG, __in DWORD, __out_opt PULONG64)
{
    Log::Debug("PipeStream: SetFilePointer is not implemented");
    return S_OK;
}

ULONG64 PipeStream::GetSize()
{
    Log::Debug("PipeStream: GetSize is not implemented");
    return (ULONG64)-1;
}

HRESULT PipeStream::SetSize(ULONG64)
{
    Log::Debug("PipeStream: SetSize is not implemented");
    return S_OK;
}

bool PipeStream::DataIsAvailable()
{
    if (m_hReadPipe == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwBytesAvailable = 0L;
    if (!PeekNamedPipe(m_hReadPipe, NULL, 0L, NULL, &dwBytesAvailable, NULL))
        return false;

    if (dwBytesAvailable > 0L)
        return true;
    return false;
}

HRESULT WINAPI
PipeStream::Peek(_Out_opt_ LPVOID lpBuffer, _In_ DWORD nBufferSize, _Out_opt_ LPDWORD lpBytesRead, _Out_opt_ LPDWORD)
{
    if (m_hReadPipe == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    if (lpBytesRead)
        *lpBytesRead = 0L;

    DWORD dwBytesRead = 0L;
    if (!PeekNamedPipe(m_hReadPipe, lpBuffer, nBufferSize, &dwBytesRead, NULL, NULL))
    {
        Log::Error("Failed PeekNamedPipe (code: {:#x})", LastWin32Error());
    }

    if (dwBytesRead > 0L && lpBytesRead)
        *lpBytesRead = dwBytesRead;
    return false;
}

HRESULT PipeStream::Close()
{
    if (m_hReadPipe == INVALID_HANDLE_VALUE && m_hWritePipe == INVALID_HANDLE_VALUE)
        return S_OK;

    if (m_hWritePipe != INVALID_HANDLE_VALUE)
    {
        HANDLE hWritePipe = INVALID_HANDLE_VALUE;
        {
            ScopedLock sl(m_cs);
            std::swap(hWritePipe, m_hWritePipe);
        }
        BYTE EmptyBuffer = 0;
        DWORD dwBytesWritten = 0L;
        WriteFile(hWritePipe, &EmptyBuffer, 0L, &dwBytesWritten, NULL);  // Signal to the other end we're closing...
        CloseHandle(hWritePipe);
        Log::Debug("CloseHandle (hFile: {:p})", hWritePipe);
        hWritePipe = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}

PipeStream::~PipeStream()
{
    if (m_hReadPipe != INVALID_HANDLE_VALUE)
    {
        HANDLE hReadPipe = INVALID_HANDLE_VALUE;
        {
            ScopedLock sl(m_cs);
            std::swap(hReadPipe, m_hReadPipe);
        }
        CloseHandle(hReadPipe);
        Log::Debug("CloseHandle (hFile: {:p})", hReadPipe);
        hReadPipe = INVALID_HANDLE_VALUE;
    }
    if (m_hWritePipe != INVALID_HANDLE_VALUE)
    {
        HANDLE hWritePipe = INVALID_HANDLE_VALUE;
        {
            ScopedLock sl(m_cs);
            std::swap(hWritePipe, m_hWritePipe);
        }

        CloseHandle(hWritePipe);
        Log::Debug("CloseHandle (hFile: {:p})", hWritePipe);
        hWritePipe = INVALID_HANDLE_VALUE;
    }
}
