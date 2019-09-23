//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "StreamMessage.h"

using namespace std;

using namespace Orc;

StreamMessage::StreamMessage(void)
    : m_hr(S_OK)
{
}

std::shared_ptr<StreamMessage> StreamMessage::MakeReadRequest(ULONGLONG dwBytesToRead)
{
    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = StreamMessage::Read;
    retval->m_ullBytesToRead = dwBytesToRead;
    return retval;
}

std::shared_ptr<StreamMessage> StreamMessage::MakeWriteRequest(const CBinaryBuffer& buffer)
{
    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = StreamMessage::Write;
    retval->m_ullBytesToWrite = buffer.GetCount();
    retval->m_Buffer = buffer;
    return retval;
}

std::shared_ptr<StreamMessage> StreamMessage::MakeWriteRequest(CBinaryBuffer&& buffer)
{
    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = StreamMessage::Write;
    retval->m_ullBytesToWrite = buffer.GetCount();

    std::swap(retval->m_Buffer, buffer);

    return retval;
}

std::shared_ptr<StreamMessage>
StreamMessage::MakeSetFilePointer(StreamRequest ReadOrWrite, DWORD dwMoveMethod, LONGLONG llDistanceToMove)
{
    if (ReadOrWrite != StreamMessage::Read && ReadOrWrite != StreamMessage::Write)
        return nullptr;

    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = static_cast<StreamRequest>(StreamMessage::SetFilePointer | ReadOrWrite);
    retval->m_dwMoveMethod = dwMoveMethod;
    retval->m_llDistanceToMove = llDistanceToMove;
    return retval;
}

std::shared_ptr<StreamMessage> StreamMessage::MakeSetSize(StreamRequest ReadOrWrite, ULONGLONG ullNewSize)
{
    if (ReadOrWrite != StreamMessage::Read && ReadOrWrite != StreamMessage::Write)
        return nullptr;

    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = static_cast<StreamRequest>(StreamMessage::SetSize | ReadOrWrite);
    retval->SetStreamSize(ullNewSize);
    return retval;
}
std::shared_ptr<StreamMessage> StreamMessage::MakeGetSize(StreamRequest ReadOrWrite)
{
    if (ReadOrWrite != StreamMessage::Read && ReadOrWrite != StreamMessage::Write)
        return nullptr;

    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = static_cast<StreamRequest>(StreamMessage::GetSize | ReadOrWrite);

    return retval;
}
std::shared_ptr<StreamMessage> StreamMessage::MakeCloseRequest(StreamRequest ReadOrWrite)
{
    if (ReadOrWrite != StreamMessage::Read && ReadOrWrite != StreamMessage::Write)
        return nullptr;

    shared_ptr<StreamMessage> retval = make_shared<StreamMessage>();

    retval->m_Request = static_cast<StreamRequest>(StreamMessage::Close | ReadOrWrite);

    return retval;
}

StreamMessage::~StreamMessage(void) {}
