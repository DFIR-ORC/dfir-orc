//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <queue>

#include <agents.h>
#include <concurrent_queue.h>

#include "CircularStorage.h"
#include "StreamMessage.h"
#include "StreamAgent.h"

#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class BufferAgent;
class LogFileWriter;

class ORCLIB_API BufferAgent : public StreamAgent
{

private:
    StreamMessage::ITarget& m_writetarget;
    std::queue<StreamMessage::Message> m_ReadQueue;
    std::queue<StreamMessage::Message> m_WriteQueue;
    CircularStorage m_Accumulating;
    ULONGLONG m_ullAccumulatedBytes = 0LL;
    bool m_bWriteClosed = false;

    HRESULT DeQueueReads();

public:
    BufferAgent(
        logger pLog,
        StreamMessage::ISource& request,
        StreamMessage::ITarget& reads_target,
        StreamMessage::ITarget& writes_target)
        : StreamAgent(std::move(pLog), request, reads_target)
        , m_writetarget(writes_target)
    {
    }

    virtual void run();

    ~BufferAgent(void);
};
}  // namespace Orc

#pragma managed(pop)
