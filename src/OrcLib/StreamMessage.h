//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "BinaryBuffer.h"

#include <agents.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API StreamMessage
{

public:
    enum StreamRequest
    {
        Read = 1,
        Write = 1 << 1,
        SetFilePointer = 1 << 2,
        SetSize = 1 << 3,
        GetSize = 1 << 4,
        Close = 1 << 5
    };

    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<std::shared_ptr<StreamMessage>>;
    using UnboundedMessagePipeline = std::pair<UnboundedMessageBuffer, UnboundedMessageBuffer>;
    using Message = std::shared_ptr<StreamMessage>;
    using ITarget = Concurrency::ITarget<Message>;
    using ITargetRef = Concurrency::ITarget<Message>&;
    using ISource = Concurrency::ISource<Message>;
    using ISourceRef = Concurrency::ISource<Message>&;
    using MessagePipeline = std::pair<ISource, ITarget>;
    using MessagePipelineRef = std::pair<ISourceRef, ITargetRef>;

private:
    StreamRequest m_Request;
    HRESULT m_hr;

    CBinaryBuffer m_Buffer;

    union
    {
        struct
        {
            ULONGLONG m_ullBytesToRead;
            ULONGLONG m_ullBytesRead;
        };
        struct
        {
            ULONGLONG m_ullBytesToWrite;
            ULONGLONG m_ullBytesWritten;
        };
        struct
        {
            DWORD m_dwMoveMethod;
            LONGLONG m_llDistanceToMove;
            LONGLONG m_llNewFilePointer;
        };
        struct
        {
            ULONGLONG m_ullSize;
        };
    };

public:
    StreamMessage(void);

    static Message MakeReadRequest(ULONGLONG dwBytesToRead);

    static Message MakeWriteRequest(const CBinaryBuffer& buffer);
    static Message MakeWriteRequest(CBinaryBuffer&& buffer);

    static Message MakeSetFilePointer(StreamRequest ReadOrWrite, DWORD m_dwMoveMethod, LONGLONG m_ullDistanceToMove);

    static Message MakeSetSize(StreamRequest ReadOrWrite, ULONGLONG ullNewSize);
    static Message MakeGetSize(StreamRequest ReadOrWrite);

    static Message MakeCloseRequest(StreamRequest ReadOrWrite);

    CBinaryBuffer& Buffer() { return m_Buffer; };

    StreamRequest Request() { return m_Request; };

    ULONGLONG BytesToRead() { return (m_Request == StreamMessage::Read ? m_ullBytesToRead : 0); };
    void SetBytesRead(ULONGLONG ullBytesRead)
    {
        if (m_Request == StreamMessage::Read)
            m_ullBytesRead = ullBytesRead;
    };

    ULONGLONG BytesToWrite() { return (m_Request == StreamMessage::Write ? m_ullBytesToWrite : 0); };
    void SetBytesWritten(ULONGLONG ullBytesWritten)
    {
        if (m_Request == StreamMessage::Write)
            m_ullBytesWritten = ullBytesWritten;
    };
    ULONGLONG BytesWritten() { return (m_Request == StreamMessage::Write ? m_ullBytesWritten : 0); };

    DWORD MoveMethod() { return (m_Request == StreamMessage::SetFilePointer) ? m_dwMoveMethod : 0; };
    LONGLONG DistanceToMove() { return (m_Request == StreamMessage::SetFilePointer) ? m_llDistanceToMove : 0; };

    void SetNewPosition(LONGLONG newPosition)
    {
        if (m_Request == StreamMessage::SetFilePointer)
            m_llNewFilePointer = newPosition;
    };
    LONGLONG GetNewPosition() { return (m_Request == StreamMessage::SetFilePointer) ? m_llNewFilePointer : 0; };

    void SetStreamSize(ULONGLONG ullSize)
    {
        if (m_Request & StreamMessage::SetSize || m_Request & StreamMessage::GetSize)
            m_ullSize = ullSize;
        else
            m_ullSize = 0LL;
    };
    ULONGLONG GetStreamSize()
    {
        if (m_Request & StreamMessage::SetSize || m_Request & StreamMessage::GetSize)
            return m_ullSize;
        else
            return 0LL;
    };

    HRESULT HResult() { return m_hr; };
    void SetHResult(HRESULT hr) { m_hr = hr; };

    ~StreamMessage(void);
};

}  // namespace Orc

#pragma managed(pop)
