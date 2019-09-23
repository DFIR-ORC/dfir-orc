//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "agents.h"

#include "StreamMessage.h"
#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API StreamAgent : public Concurrency::agent
{
protected:
    logger _L_;

    std::shared_ptr<ByteStream> m_Encapsulated;

    StreamMessage::ITarget& m_target;
    StreamMessage::ISource& m_source;

    bool SendResult(const StreamMessage::Message& request)
    {
        return Concurrency::send<StreamMessage::Message>(m_target, request);
    }
    StreamMessage::Message GetRequest() { return Concurrency::receive<StreamMessage::Message>(m_source); }

    virtual void OnClose() {};

public:
    StreamAgent(logger pLog, StreamMessage::ISource& source, StreamMessage::ITarget& target)
        : _L_(std::move(pLog))
        , m_source(source)
        , m_target(target)
    {
    }

    HRESULT SetEncapsulatedStream(const std::shared_ptr<ByteStream>& aStream)
    {
        m_Encapsulated = aStream;
        return S_OK;
    }

    virtual void run();

    ~StreamAgent(void);
};

}  // namespace Orc

#pragma managed(pop)
