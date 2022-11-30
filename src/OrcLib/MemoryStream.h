//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;

class MemoryStream : public ByteStream
{
    static const size_t kDefaultReservedBytes = sizeof(size_t) == 8 ? 1024 * 1024 * 100 : 1024 * 1024 * 5;

protected:
    __field_ecount_part(m_cbBufferCommitSize, m_cbBuffer) PBYTE m_pBuffer;
    size_t m_cbBuffer;
    size_t m_cbReservedBytes;
    size_t m_cbBufferCommitSize;
    size_t m_dwCurrFilePointer;
    bool m_bReadOnly;

    HRESULT SetBufferSize(size_t dwCommitSize, size_t dwReserveSize);

    HRESULT CommitBuffer(size_t dwPosition, size_t dwCommitSize);

public:
    MemoryStream();
    ~MemoryStream();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {
        if (m_pBuffer)
            return S_OK;
        else
            return S_FALSE;
    };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return m_bReadOnly ? S_FALSE : S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };

    //
    // CByteStream implementation
    //
    STDMETHOD(OpenForReadWrite)(DWORD dwSize = 4096, DWORD dwReservedBytes = kDefaultReservedBytes);

    STDMETHOD(OpenForReadOnly)(__in PVOID pBuffer, __in size_t cbBuffer);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);

    STDMETHOD(Clone)(std::shared_ptr<ByteStream>& clone);
    STDMETHOD(Close)();

    HRESULT Duplicate(const MemoryStream& other);

    CBinaryBuffer GetBuffer();
    const CBinaryBuffer GetConstBuffer() const;
    void GrabBuffer(CBinaryBuffer& buffer);
};

}  // namespace Orc

#pragma managed(pop)
