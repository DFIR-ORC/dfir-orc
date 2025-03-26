//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include <filesystem>

#include "ByteStream.h"
#include "CriticalSection.h"

#pragma managed(push, off)

namespace Orc {

class FileStream : public ByteStream
{
public:
    FileStream()
        : ByteStream()
    {
    }
    ~FileStream();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {
        if (m_hFile != INVALID_HANDLE_VALUE)
            return S_OK;
        else
            return S_FALSE;
    };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };

    HRESULT OpenFile(
        __in PCWSTR pwzPath,
        __in DWORD dwDesiredAccess,
        __in DWORD dwSharedMode,
        __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
        __in DWORD dwCreationDisposition,
        __in DWORD dwFlagsAndAttributes,
        __in_opt HANDLE hTemplate);

    HRESULT OpenFile(
        __in const std::filesystem::path& path,
        __in DWORD dwDesiredAccess,
        __in DWORD dwSharedMode,
        __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
        __in DWORD dwCreationDisposition,
        __in DWORD dwFlagsAndAttributes,
        __in_opt HANDLE hTemplate);

    HRESULT ReadFrom(__in PCWSTR pwzPath, bool bDeleteOnClose = false)
    {
        return OpenFile(
            pwzPath,
            GENERIC_READ,
            FILE_SHARE_READ | (bDeleteOnClose ? FILE_SHARE_DELETE : 0LU),
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL
                | (bDeleteOnClose
                   ? FILE_FLAG_DELETE_ON_CLOSE : 0LU),
            NULL);
    }
    HRESULT WriteTo(__in PCWSTR pwzPath, bool bDeleteOnClose = false)
    {
        return OpenFile(
            pwzPath,
            GENERIC_WRITE,
            bDeleteOnClose ? FILE_SHARE_DELETE : 0LU,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | (bDeleteOnClose ? FILE_FLAG_DELETE_ON_CLOSE : 0LU),
            NULL);
    }

    HRESULT CopyHandle(HANDLE hFile);
    HRESULT OpenHandle(HANDLE hFile);

    HRESULT Duplicate(const FileStream& other);

    STDMETHOD(CreateNew)
    (__in_opt PCWSTR pwszExt,
     __in DWORD dwSharedMode,
     __in DWORD dwFlags,
     __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytesToRead,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);
    HANDLE GetHandle() { return m_hFile; };

    const std::wstring& Path() const { return m_strPath; }

    STDMETHOD(Clone)(std::shared_ptr<ByteStream>& clone);
    STDMETHOD(Close)();

protected:
    HANDLE m_hFile = INVALID_HANDLE_VALUE;
    std::wstring m_strPath;
    CriticalSection m_cs;
};

}  // namespace Orc

#pragma managed(pop)
