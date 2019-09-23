//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"
#include "BufferAgent.h"

#include "LogFileWriter.h"

using namespace Concurrency;

static const auto MAX_ACCUMULATING_STORAGE = (1024 * 1024 * 8);  // 8MB
static const auto INITIAL_ACCUMULATING_STORAGE = (1024 * 4);  // 4KB

using namespace Orc;

HRESULT BufferAgent::DeQueueReads()
{
    HRESULT hr = E_FAIL;

    if (m_ReadQueue.empty())
        return S_OK;

    auto readrequest = m_ReadQueue.front();

    if (readrequest)
    {
        do
        {
            size_t nbBytesToRead = 0L;
            if (m_bWriteClosed)
            {
                m_ReadQueue.pop();
                if (readrequest->BytesToRead() <= m_Accumulating.Size())
                {
                    nbBytesToRead = (size_t)readrequest->BytesToRead();
                }
                else
                {
                    nbBytesToRead = m_Accumulating.Size();
                }
            }
            else
            {
                if (readrequest->BytesToRead() <= m_Accumulating.Size())
                {
                    m_ReadQueue.pop();
                    nbBytesToRead = (size_t)readrequest->BytesToRead();
                }
            }

            if (nbBytesToRead)
            {
                if (FAILED(hr = m_Accumulating.PopBytes(readrequest->Buffer(), nbBytesToRead)))
                {
                    readrequest->SetHResult(hr);
                    break;
                }
                SendResult(readrequest);
            }
            else if (m_bWriteClosed && m_Accumulating.Size() == 0)
            {
                readrequest->Buffer().SetCount(0);
                SendResult(readrequest);
            }
            else
                break;

            if (!m_ReadQueue.empty())
                readrequest = m_ReadQueue.front();
            else
                readrequest = nullptr;

        } while (readrequest);
    }
    return S_OK;
}

void BufferAgent::run()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Accumulating.InitializeStorage(MAX_ACCUMULATING_STORAGE, INITIAL_ACCUMULATING_STORAGE)))
    {
        // Could not start receiving agent, cancel.
        log::Error(_L_, hr, L"Could not initialize accumulating storage for buffer agent\r\n");
        cancel();
        return;
    }

    StreamMessage::Message request;
    bool bDone = false;
    {
        request = GetRequest();
    }

    while (request)
    {
        switch ((DWORD)request->Request())
        {
            case StreamMessage::Read:
                m_ReadQueue.push(request);
                break;

            case StreamMessage::Write:

                if (FAILED(DeQueueReads()))
                    break;

                if (!m_WriteQueue.empty())
                {
                    auto writerequest = m_WriteQueue.front();

                    if (writerequest)
                    {
                        do
                        {
                            if (SUCCEEDED(hr = m_Accumulating.PushBytes(writerequest->Buffer())))
                            {
                                writerequest->SetHResult(hr);
                                m_ullAccumulatedBytes += writerequest->Buffer().GetCount();
                                Concurrency::send(&m_writetarget, writerequest);
                                m_WriteQueue.pop();

                                if (!m_WriteQueue.empty())
                                    writerequest = m_ReadQueue.front();
                                else
                                    writerequest = nullptr;
                            }
                            else
                            {
                                writerequest = nullptr;
                            }

                        } while (writerequest);
                    }
                }

                if (m_WriteQueue.empty())
                {
                    if (FAILED(hr = m_Accumulating.PushBytes(request->Buffer())))
                    {
                        m_WriteQueue.push(request);
                    }
                    else
                    {
                        request->SetHResult(hr);
                        m_ullAccumulatedBytes += request->Buffer().GetCount();
                        Concurrency::send(&m_writetarget, request);
                    }
                }
                else
                {
                    m_WriteQueue.push(request);
                }
                break;
            case static_cast<StreamMessage::StreamRequest>(StreamMessage::SetFilePointer | StreamMessage::Read):

                // Only setting the stream to its start is supported...
                if (request->DistanceToMove() != 0LL && request->MoveMethod() != FILE_BEGIN)
                    request->SetHResult(E_INVALIDARG);
                else
                    request->SetHResult(S_OK);

                Concurrency::send(&m_target, request);
                break;
            case static_cast<StreamMessage::StreamRequest>(StreamMessage::SetFilePointer | StreamMessage::Write):

                // Only setting the stream to its start is supported...
                if (request->DistanceToMove() != 0LL && request->MoveMethod() != FILE_BEGIN)
                    request->SetHResult(E_INVALIDARG);
                else
                    request->SetHResult(S_OK);

                Concurrency::send(&m_writetarget, request);
                break;
            case static_cast<StreamMessage::StreamRequest>(StreamMessage::GetSize | StreamMessage::Read):
            {
                ULONGLONG ullSize = m_ullAccumulatedBytes;
                request->SetHResult(S_OK);
                request->SetStreamSize(ullSize);
                Concurrency::send(&m_target, request);
            }
            break;
            case static_cast<StreamMessage::StreamRequest>(StreamMessage::GetSize | StreamMessage::Write):
            {
                ULONGLONG ullSize = m_ullAccumulatedBytes;
                request->SetHResult(S_OK);
                request->SetStreamSize(ullSize);
                Concurrency::send(&m_writetarget, request);
            }
            break;
            case static_cast<StreamMessage::StreamRequest>(StreamMessage::Close | StreamMessage::Read):
                if (m_bWriteClosed)
                {
                    // writing close was already received...
                    bDone = true;
                    Concurrency::send(&m_target, request);
                }
                break;
            case static_cast<StreamMessage::StreamRequest>(StreamMessage::Close | StreamMessage::Write):
            {
                Concurrency::send(&m_writetarget, request);
                m_bWriteClosed = true;
            }
            break;
        }

        // dequeue as many reads as possible
        DeQueueReads();

        if (bDone)
            request = nullptr;
        else
        {
            request = GetRequest();
        }
    }
    done();
    m_Accumulating.UninitializeStorage();
}

BufferAgent::~BufferAgent(void) {}
