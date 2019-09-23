//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "StreamAgent.h"

#include "LogFileWriter.h"

using namespace Orc;

void StreamAgent::run()
{
    if (m_Encapsulated == nullptr)
    {
        log::Error(_L_, E_UNEXPECTED, L"run has been called but agent does not ecaplusate any stream\r\n");
        done();
        return;
    }

    StreamMessage::Message request;
    bool bDone = false;
    {
        request = GetRequest();
    }

    while (request)
    {
        switch (request->Request())
        {
            case StreamMessage::Read:
            {
                request->Buffer().SetCount(static_cast<size_t>(request->BytesToRead()));
                ULONGLONG ullRead = 0LL;
                request->SetHResult(
                    m_Encapsulated->Read(request->Buffer().GetData(), request->BytesToRead(), &ullRead));
                if (SUCCEEDED(request->HResult()))
                    request->SetBytesRead(ullRead);
                Concurrency::send(&m_target, request);
            }
            break;
            case StreamMessage::Write:
            {
                ULONGLONG ullWritten = 0LL;
                request->SetHResult(
                    m_Encapsulated->Write(request->Buffer().GetData(), request->BytesToWrite(), &ullWritten));
                if (SUCCEEDED(request->HResult()))
                    request->SetBytesWritten(ullWritten);
                Concurrency::send(&m_target, request);
            }
            break;
            case StreamMessage::SetFilePointer:
            {
                ULONGLONG ullNewPosition = 0LL;
                request->SetHResult(
                    m_Encapsulated->SetFilePointer(request->DistanceToMove(), request->MoveMethod(), &ullNewPosition));
                if (SUCCEEDED(request->HResult()))
                    request->SetNewPosition(ullNewPosition);
                Concurrency::send(&m_target, request);
            }
            break;
            case StreamMessage::SetSize:
            {
                request->SetHResult(m_Encapsulated->SetSize(request->GetStreamSize()));
                Concurrency::send(&m_target, request);
            }
            break;
            case StreamMessage::GetSize:
            {
                ULONGLONG ullSize = m_Encapsulated->GetSize();
                request->SetHResult(S_OK);
                request->SetStreamSize(ullSize);
                Concurrency::send(&m_target, request);
            }
            break;
            case StreamMessage::Close:
            {
                OnClose();
                request->SetHResult(m_Encapsulated->Close());
                Concurrency::send(&m_target, request);
                bDone = true;
            }
            break;
        }

        if (bDone)
            request = nullptr;
        else
        {
            request = GetRequest();
        }
    }
    done();
}

StreamAgent::~StreamAgent(void) {}
