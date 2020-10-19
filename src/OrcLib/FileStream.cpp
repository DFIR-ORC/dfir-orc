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
        Log::Trace(L"CloseHandle (hFile: {:p})", hFile);
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
        Log::Error(L"Failed CreateFile for '{}' (code: {:#x})", pwszPath, hr);
        return hr;
    }

    Log::Trace(L"CreateFile({}) succeeded (hFile={:p})", pwszPath, hFile);
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFile, hFile);
        m_strPath.assign(pwszPath);
    }

    return S_OK;
}

__data_entrypoint(File) HRESULT FileStream::OpenFile(
    __in const std::filesystem::path& path,
    __in DWORD dwDesiredAccess,
    __in DWORD dwSharedMode,
    __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
    __in DWORD dwCreationDisposition,
    __in DWORD dwFlagsAndAttributes,
    __in_opt HANDLE hTemplate)
{
    return OpenFile(
        path.c_str(),
        dwDesiredAccess,
        dwSharedMode,
        pSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplate);
}

HRESULT FileStream::CopyHandle(HANDLE hFile)
{
    if (m_hFile != INVALID_HANDLE_VALUE)
        Close();

    if (hFile == INVALID_HANDLE_VALUE)
    {
        Log::Error("Failed CopyHandle (hFile: {:p}, code: {:#x})", hFile, LastWin32Error());
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
        Log::Error("Failed OpenHandle (hFile: {:p}, code: {:#x})", hFile, LastWin32Error());
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
        Log::Error("Failed Duplicate (hFile: {:p}, code: {:#x})", other.m_hFile, LastWin32Error());
        return E_INVALIDARG;
    }

    const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>(true);

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

    *pcbBytesRead = 0;

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;

    DWORD dwBytesRead = 0;
    if (!ReadFile(m_hFile, pBuffer, (DWORD)cbBytesToRead, &dwBytesRead, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed ReadFile (code: {:#x})", hr);
        return hr;
    }

    Log::Trace(L"ReadFile read {} bytes (hFile: {:p})", dwBytesRead, m_hFile);

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

    DWORD cbBytesWritten = 0;
    if (cbBytes > MAXDWORD)
    {
        Log::Error("Write: Too many bytes to Write");
        return E_INVALIDARG;
    }

    if (!WriteFile(m_hFile, pBuffer, static_cast<DWORD>(cbBytes), &cbBytesWritten, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed WriteFile (code: {:#x})", hr);
        return hr;
    }

    Log::Trace("WriteFile {} bytes succeeded (hFile: {:p})", cbBytesWritten, m_hFile);
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
    LARGE_INTEGER liDistanceToMove = {0};
    LARGE_INTEGER liNewFilePointer = {0};

    liDistanceToMove.QuadPart = lDistanceToMove;

    if (!SetFilePointerEx(m_hFile, liDistanceToMove, &liNewFilePointer, dwMoveMethod))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed SetFilePointerEx (code: {:#x})", hr);
        return hr;
    }

    if (pqwCurrPointer)
    {
        *pqwCurrPointer = liNewFilePointer.QuadPart;
    }

    Log::Trace("SetFilePointer succeeded (hFile: {:p}, pointer: {:#x})", m_hFile, liNewFilePointer.QuadPart);
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
    LARGE_INTEGER Size = {0};

    if (!GetFileSizeEx(m_hFile, &Size))
    {

        Log::Error("Failed GetFileSizeEx (code: {:#x})", LastWin32Error());
        return ULONG64(-1);
    }

    Log::Trace("GetFileSizeEx succeeded (hFile: {:p}, filesize: {})", m_hFile, Size.QuadPart);
    return Size.QuadPart;
}

HRESULT FileStream::SetSize(ULONG64 ullNewSize)
{
    LARGE_INTEGER liCurrentFilePointer = {0};
    LARGE_INTEGER liDistanceToMove = {0};
    liDistanceToMove.QuadPart = 0;

    // Saving current position
    if (!(SetFilePointerEx(m_hFile, liDistanceToMove, &liCurrentFilePointer, FILE_CURRENT)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed SetFilePointerEx (code: {:#x})", hr);
        return hr;
    }

    // Setting position to new file end
    LARGE_INTEGER liNewFileEnd = {0};
    liNewFileEnd.QuadPart = ullNewSize;
    if (!(SetFilePointerEx(m_hFile, liNewFileEnd, &liCurrentFilePointer, FILE_BEGIN)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed SetFilePointerEx (code: {:#x})", hr);
        return hr;
    }

    // Setting the file size at this position
    if (!(SetEndOfFile(m_hFile)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed SetEndOfFile (code: {:#x})", hr);
        return hr;
    }

    // Restoring the file position
    if (!(SetFilePointerEx(m_hFile, liCurrentFilePointer, &liCurrentFilePointer, FILE_BEGIN)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed SetFilePointerEx (code: {:#x})", hr);
        return hr;
    }
    return S_OK;
}

FileStream::~FileStream()
{
    Close();
}
