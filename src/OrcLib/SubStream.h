//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
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

class SubStream : public ByteStream
{
public:
    SubStream(ByteStream& stream, uint64_t chunkOffset, uint64_t chunkSize);
    ~SubStream();

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

private:
    ByteStream& m_stream;
    uint64_t m_chunkOffset;
    uint64_t m_chunkSize;
};

namespace Guard {

template <typename StreamT>
class SubStream final : public Orc::SubStream
{
public:
    SubStream(StreamT stream, uint64_t chunkOffset, size_t chunkSize)
        : SubStream(*MetaPtr<StreamT>(stream).get(), chunkOffset, chunkSize)
        , m_stream(std::move(stream))
    {
    }

private:
    MetaPtr<StreamT> m_stream;
};

}  // namespace Guard

}  // namespace Orc

#pragma managed(pop)
