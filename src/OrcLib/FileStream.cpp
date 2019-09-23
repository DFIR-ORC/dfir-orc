//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "FileStream.h"

#include "Temporary.h"

#include "LogFileWriter.h"

#include "Kernel32Extension.h"

using namespace Orc;

/*
    CFileStream::Close

    Frees opened file handle, if any
*/
HRESULT FileStream::Close()
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFile, hFile);
    }

    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
        log::Verbose(_L_, L"CloseHandle (hFile=0x%lx)\r\n", hFile);
        hFile = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}

/*
    CFileStream::CreateNew

    Creates a file for the stream. All subsequent calls to stream functions will
    then operate on the opened file
*/
HRESULT FileStream::CreateNew(
    __in_opt PCWSTR pwszExt,
    __in DWORD dwSharedMode,
    __in DWORD dwFlags,
    __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes)
{
    WCHAR wszFilePath[MAX_PATH];
    HRESULT hr = E_FAIL;

    wszFilePath[0] = 0;

    if (FAILED(
            hr =
                UtilGetTempFile(NULL, NULL, pwszExt ? pwszExt : L".txt", wszFilePath, ARRAYSIZE(wszFilePath), NULL, 0)))
        return hr;

    if (FAILED(
            hr = OpenFile(
                wszFilePath,
                GENERIC_READ | GENERIC_WRITE,
                dwSharedMode,
                pSecurityAttributes,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | dwFlags,
                NULL)))
        return hr;

    return S_OK;
}

/*
    OpenFile

    Opens up a file for the stream. All subsequent calls to stream functions
    will then operate on the opened file

    Parameters:
        See CreateFile() documentation
*/
__data_entrypoint(File) HRESULT FileStream::OpenFile(
    __in PCWSTR pwszPath,
    __in DWORD dwDesiredAccess,
    __in DWORD dwSharedMode,
    __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
    __in DWORD dwCreationDisposition,
    __in DWORD dwFlagsAndAttributes,
    __in_opt HANDLE hTemplate)
{
    HRESULT hr = E_FAIL;

    if (m_hFile != INVALID_HANDLE_VALUE)
        Close();

    HANDLE hFile = CreateFile(
        pwszPath,
        dwDesiredAccess,
        dwSharedMode,
        pSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplate);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"CreateFile(%s) failed\r\n", pwszPath);
        return hr;
    }
    log::Verbose(_L_, L"CreateFile(%s) succeeded (hFile=0x%lx)\r\n", pwszPath, hFile);
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFile, hFile);
    }

    return S_OK;
}

HRESULT FileStream::CopyHandle(HANDLE hFile)
{
    if (m_hFile != INVALID_HANDLE_VALUE)
        Close();

    if (hFile == INVALID_HANDLE_VALUE)
    {
        log::Error(_L_, E_INVALIDARG, L"OpenHandle(%p) failed\r\n", hFile);
        return E_INVALIDARG;
    }
    HANDLE hDupHandle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(
            GetCurrentProcess(), hFile, GetCurrentProcess(), &hDupHandle, 0L, FALSE, DUPLICATE_SAME_ACCESS))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFile, hDupHandle);
    }
    return S_OK;
}

HRESULT FileStream::OpenHandle(HANDLE hFile)
{
    if (m_hFile != INVALID_HANDLE_VALUE)
        Close();

    if (hFile == INVALID_HANDLE_VALUE)
    {
        log::Error(_L_, E_INVALIDARG, L"OpenHandle(%p) failed\r\n", hFile);
        return E_INVALIDARG;
    }
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFile, hFile);
    }
    return S_OK;
}

HRESULT FileStream::Duplicate(const FileStream& other)
{
    if (m_hFile != INVALID_HANDLE_VALUE)
        Close();

    if (other.m_hFile == INVALID_HANDLE_VALUE)
    {
        log::Error(_L_, E_INVALIDARG, L"OpenHandle(%p) failed\r\n", other.m_hFile);
        return E_INVALIDARG;
    }

    const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>(_L_, true);

    HANDLE hFile = INVALID_HANDLE_VALUE;
    if ((hFile = pk32->ReOpenFile(other.m_hFile, GENERIC_READ, FILE_SHARE_READ, FILE_FLAG_SEQUENTIAL_SCAN))
        == INVALID_HANDLE_VALUE)
    {
        if (!DuplicateHandle(
                GetCurrentProcess(), other.m_hFile, GetCurrentProcess(), &m_hFile, 0L, FALSE, DUPLICATE_SAME_ACCESS))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFile, hFile);
    }

    return S_OK;
}

/*
    CFileStream::Read

    Reads data from the stream

    Parameters:
        pBuffer         -   Pointer to buffer the recieves the data
        cbBytesToRead   -   Number of bytes to read into pBuffer
        pcbBytesRead    -   Will receive the number of bytes placed in pBuffer
*/
__data_entrypoint(File) HRESULT FileStream::Read(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (m_hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    *pcbBytesRead = 0;

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;

    DWORD dwBytesRead = 0;
    if (!ReadFile(m_hFile, pBuffer, (DWORD)cbBytesToRead, &dwBytesRead, NULL))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"ReadFile failed\r\n");
        return hr;
    }

    log::Verbose(_L_, L"ReadFile read %d bytes (hFile=0x%lx)\r\n", dwBytesRead, m_hFile);

    *pcbBytesRead = dwBytesRead;
    return S_OK;
}

/*
    CFileStream::Write

    Writes data to the stream

    Parameters:
        pBuffer         -   Pointer to buffer to write from
        cbBytes         -   Number of bytes to write from pBuffer to the stream
        pcbBytesRead    -   Will recieve the number of bytes placed in pBuffer
*/
HRESULT
FileStream::Write(__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (m_hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    DWORD cbBytesWritten = 0;
    if (cbBytes > MAXDWORD)
    {
        log::Error(_L_, E_INVALIDARG, L"Too many bytes to Write\r\n");
        return E_INVALIDARG;
    }

    if (!WriteFile(m_hFile, pBuffer, static_cast<DWORD>(cbBytes), &cbBytesWritten, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"WriteFile failed\r\n");
        return hr;
    }
    log::Verbose(_L_, L"WriteFile %d bytes succeeded (hFile=0x%lx)\r\n", cbBytesWritten, m_hFile);
    *pcbBytesWritten = cbBytesWritten;
    return S_OK;
}

/*
    CFileStream:SetFilePointer

    Seeks the to the specified location in the file
*/
HRESULT
FileStream::SetFilePointer(__in LONGLONG lDistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pqwCurrPointer)
{
    if (m_hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    LARGE_INTEGER liDistanceToMove = {0};
    LARGE_INTEGER liNewFilePointer = {0};

    liDistanceToMove.QuadPart = lDistanceToMove;

    if (!SetFilePointerEx(m_hFile, liDistanceToMove, &liNewFilePointer, dwMoveMethod))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"SetFilePointerEx failed\r\n");
        return hr;
    }

    if (pqwCurrPointer)
    {
        *pqwCurrPointer = liNewFilePointer.QuadPart;
    }
    log::Verbose(_L_, L"SetFilePointer succeeded (hFile=0x%lx, pointer=%I64x)\r\n", m_hFile, liNewFilePointer.QuadPart);
    return S_OK;
}

/*
    CFileStream::GetSize

    Returns the length of the file

    Return:
        The file size or -1 if there was an error
*/
ULONG64 FileStream::GetSize()
{
    if (m_hFile == INVALID_HANDLE_VALUE)
        return ULONG64(-1);

    LARGE_INTEGER Size = {0};

    if (!GetFileSizeEx(m_hFile, &Size))
    {
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"GetFileSizeEx failed\r\n");
        return ULONG64(-1);
    }
    log::Verbose(_L_, L"GetFileSizeEx succeeded (hFile=0x%lx, filesize=%I64x)\r\n", m_hFile, Size.QuadPart);
    return Size.QuadPart;
}

HRESULT FileStream::SetSize(ULONG64 ullNewSize)
{
    if (m_hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);

    LARGE_INTEGER liCurrentFilePointer = {0};
    LARGE_INTEGER liDistanceToMove = {0};
    liDistanceToMove.QuadPart = 0;

    // Saving current position
    if (!(SetFilePointerEx(m_hFile, liDistanceToMove, &liCurrentFilePointer, FILE_CURRENT)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"SetFilePointerEx failed\r\n");
        return hr;
    }

    // Setting position to new file end
    LARGE_INTEGER liNewFileEnd = {0};
    liNewFileEnd.QuadPart = ullNewSize;
    if (!(SetFilePointerEx(m_hFile, liDistanceToMove, &liCurrentFilePointer, FILE_BEGIN)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"SetFilePointerEx failed\r\n");
        return hr;
    }

    // Setting the file size at this position
    if (!(SetEndOfFile(m_hFile)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"SetEndOfFile failed\r\n");
        return hr;
    }

    // Restoring the file position
    if (!(SetFilePointerEx(m_hFile, liCurrentFilePointer, &liCurrentFilePointer, FILE_BEGIN)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"SetFilePointerEx failed\r\n");
        return hr;
    }
    return S_OK;
}

FileStream::~FileStream()
{
    Close();
}
