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

#include "StreamMessage.h"

#include <boost/logic/tribool.hpp>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ConcurrentStream : public ByteStream
{
    friend class StreamAgent;

private:
    StreamMessage::ITarget* m_request;
    StreamMessage::ISource* m_result;

    boost::logic::tribool m_bRead;
    boost::logic::tribool m_bWrite;
    boost::logic::tribool m_bSeek;

public:
    ConcurrentStream(logger pLog, StreamMessage::ITarget* request = nullptr, StreamMessage::ISource* result = nullptr)
        : ByteStream(std::move(pLog))
        , m_request(request)
        , m_result(result) {};

    HRESULT SetBuffers(StreamMessage::ITarget* request = nullptr, StreamMessage::ISource* result = nullptr)
    {
        if (m_request != nullptr || m_result != nullptr)
            return E_NOT_VALID_STATE;
        m_request = request;
        m_result = result;
        return S_OK;
    }

    STDMETHOD(OpenToRead)()
    {
        m_bRead = true;
        return S_OK;
    };
    STDMETHOD(OpenToWrite)()
    {
        m_bWrite = true;
        return S_OK;
    };

    STDMETHOD(IsOpen)() { return (m_request != nullptr && m_result != nullptr) ? true : false; };
    STDMETHOD(CanRead)() { return m_bRead ? S_OK : S_FALSE; };
    STDMETHOD(CanWrite)() { return m_bWrite ? S_OK : S_FALSE; };
    STDMETHOD(CanSeek)() { return m_bSeek ? S_OK : S_FALSE; };

    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64);

    STDMETHOD(Close)();
};
}  // namespace Orc

#pragma managed(pop)
