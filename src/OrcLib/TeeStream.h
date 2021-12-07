//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once
#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class TeeStream : public ByteStream
{
protected:
    std::vector<std::shared_ptr<ByteStream>> m_Streams;

public:
    TeeStream()
        : ByteStream() {};

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {

        auto it = std::find_if_not(
            std::begin(m_Streams), std::end(m_Streams), [](const auto& stream) { return stream->IsOpen(); });
        if (it != std::end(m_Streams))
            return S_FALSE;
        return S_OK;
    };
    STDMETHOD(CanRead)() { return S_FALSE; };
    STDMETHOD(CanWrite)()
    {

        auto it = std::find_if_not(
            std::begin(m_Streams), std::end(m_Streams), [](const auto& stream) { return stream->CanWrite() == S_OK; });
        if (it != std::end(m_Streams))
            return S_FALSE;
        return S_OK;
    };
    STDMETHOD(CanSeek)()
    {
        auto it = std::find_if_not(
            std::begin(m_Streams), std::end(m_Streams), [](const auto& stream) { return stream->CanSeek() == S_OK; });
        if (it != std::end(m_Streams))
            return S_FALSE;
        return S_OK;
    };
    //
    // CByteStream implementation
    //
    STDMETHOD(Open)(std::vector<std::shared_ptr<ByteStream>>&& Streams);

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

    STDMETHOD(Close)();

    virtual ~TeeStream();
};
}  // namespace Orc
#pragma managed(pop)
