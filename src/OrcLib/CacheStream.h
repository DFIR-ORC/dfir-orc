//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ByteStream.h"
#include "Utils/MetaPtr.h"

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;

class CacheStream : public ByteStream
{
public:
    CacheStream(ByteStream& stream, size_t cacheSize);
    ~CacheStream();
    HRESULT Open();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)() { return m_stream.IsOpen(); };

    STDMETHOD(CanRead)() { return m_stream.CanRead(); };
    STDMETHOD(CanWrite)() { return S_FALSE; };
    STDMETHOD(CanSeek)() { return m_stream.CanSeek(); };

    //
    // CByteStream implementation
    //
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

    STDMETHOD(Clone)(ByteStream& clone);
    STDMETHOD(Close)();

    HRESULT Duplicate(const CacheStream& other);

private:
    ByteStream& m_stream;
    uint64_t m_streamOffset;
    uint64_t m_offset;
    std::vector<uint8_t> m_cache;
    size_t m_cacheUse;
    size_t m_cacheSize;
    uint64_t m_cacheOffset;
};

namespace Guard {

template <typename StreamT>
class CacheStream final : public Orc::CacheStream
{
public:
    CacheStream(StreamT stream, size_t cache_size)
        : CacheStream(*MetaPtr<StreamT>(stream).get(), cache_size)
        , m_stream(std::move(stream))
    {
    }

private:
    MetaPtr<StreamT> m_stream;
};

}  // namespace Guard

}  // namespace Orc

#pragma managed(pop)
