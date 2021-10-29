//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"

#include <filesystem>
#include <memory>
#include <string>

#pragma managed(push, off)

namespace Orc {

class MemoryStream;
class FileStream;

class ORCLIB_API TemporaryStream : public ByteStream
{
private:
    std::shared_ptr<MemoryStream> m_pMemStream;

    std::shared_ptr<FileStream> m_pFileStream;
    std::wstring m_strFileName;

    DWORD m_dwMemThreshold;
    std::wstring m_strTemp;
    std::wstring m_strIdentifier;

    bool m_bReleaseOnClose = true;

    HRESULT MoveToFileStream(const std::shared_ptr<ByteStream>& aStream);

public:
    TemporaryStream()
        : ByteStream()
        , m_dwMemThreshold(0L)
    {
    }

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)() { return m_pMemStream || m_pFileStream ? S_OK : S_FALSE; };
    STDMETHOD(CanRead)();
    STDMETHOD(CanWrite)();
    STDMETHOD(CanSeek)();

    STDMETHOD(Open)
    (const std::wstring& strTempDir, const std::wstring& strID, DWORD dwMemThreshold, bool bReleaseOnClose = true);

    STDMETHODIMP Open(const std::filesystem::path& output, DWORD dwMemThreshold, bool bReleaseOnClose = true);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64);

    STDMETHOD(Close)();

    STDMETHOD(MoveTo)(LPCWSTR lpszNewFileName);
    STDMETHOD(MoveTo)(const std::shared_ptr<ByteStream> pStream);

    STDMETHOD(IsMemoryStream)();
    STDMETHOD(IsFileStream)();

    ~TemporaryStream(void);
};

}  // namespace Orc

#pragma managed(pop)
