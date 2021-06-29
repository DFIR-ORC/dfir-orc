//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "MemoryStream.h"

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;

class ORCLIB_API CacheStream : public ByteStream
{
public:
    using CacheBuffer = std::array<uint8_t, 512000>;

    CacheStream(std::shared_ptr<ByteStream> stream);
    ~CacheStream();
    HRESULT Open();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)() { return m_stream->IsOpen(); };

    STDMETHOD(CanRead)() { return m_stream->CanRead(); };
    STDMETHOD(CanWrite)() { return S_FALSE; };
    STDMETHOD(CanSeek)() { return m_stream->CanSeek(); };

    //
    // CByteStream implementation
    //
    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);

    STDMETHOD(Clone)(std::shared_ptr<ByteStream>& clone);
    STDMETHOD(Close)();

    HRESULT Duplicate(const CacheStream& other);

private:
    std::shared_ptr<ByteStream> m_stream;
    uint64_t m_streamOffset;
    CacheBuffer m_cache;
    size_t m_cacheSize;
    uint64_t m_offset;
    uint64_t m_cacheLoadOffset;
};

}  // namespace Orc

#pragma managed(pop)
