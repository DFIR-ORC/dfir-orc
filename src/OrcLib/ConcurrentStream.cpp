//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ConcurrentStream.h"

using namespace Orc;

STDMETHODIMP ConcurrentStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    if (m_request == nullptr || m_result == nullptr)
        return E_NOT_VALID_STATE;

    auto request = StreamMessage::MakeReadRequest(cbBytes);

    Concurrency::send<StreamMessage::Message>(m_request, request);

    auto result = Concurrency::receive<StreamMessage::Message>(m_result);

    if (FAILED(result->HResult()))
    {
        if (pcbBytesRead)
            *pcbBytesRead = 0LL;
        return result->HResult();
    }

    const CBinaryBuffer& buffer = result->Buffer();

    if (buffer.GetCount())
    {
        CopyMemory(pBuffer, buffer.GetData(), std::min(static_cast<size_t>(cbBytes), buffer.GetCount()));
        if (pcbBytesRead)
            *pcbBytesRead = std::min<ULONGLONG>(cbBytes, buffer.GetCount());
    }

    return S_OK;
}

STDMETHODIMP ConcurrentStream::Write(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesWritten)
{
    if (m_request == nullptr || m_result == nullptr)
        return E_NOT_VALID_STATE;

    CBinaryBuffer buffer;
    buffer.SetData((LPBYTE)pBuffer, static_cast<size_t>(cbBytes));

    auto request = StreamMessage::MakeWriteRequest(std::move(buffer));

    Concurrency::send<StreamMessage::Message>(m_request, request);

    auto result = Concurrency::receive<StreamMessage::Message>(m_result);

    if (FAILED(result->HResult()))
    {
        if (pcbBytesWritten)
            *pcbBytesWritten = 0LL;
        return result->HResult();
    }
    if (pcbBytesWritten)
        *pcbBytesWritten = result->BytesWritten();
    return S_OK;
}

STDMETHODIMP
ConcurrentStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    if (m_request == nullptr || m_result == nullptr)
        return E_NOT_VALID_STATE;

    if (m_bRead)
    {
        auto request =
            StreamMessage::MakeSetFilePointer(StreamMessage::StreamRequest::Read, dwMoveMethod, DistanceToMove);
        Concurrency::send<StreamMessage::Message>(m_request, request);

        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            return result->HResult();
        }
        if (pCurrPointer)
            *pCurrPointer = result->GetNewPosition();
    }
    if (m_bWrite)
    {
        auto request =
            StreamMessage::MakeSetFilePointer(StreamMessage::StreamRequest::Write, dwMoveMethod, DistanceToMove);
        Concurrency::send<StreamMessage::Message>(m_request, request);

        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            return result->HResult();
        }
        if (pCurrPointer)
            *pCurrPointer = result->GetNewPosition();
    }

    return S_OK;
}

ULONG64 ConcurrentStream::GetSize()
{
    if (m_request == nullptr || m_result == nullptr)
        return E_NOT_VALID_STATE;

    if (m_bRead)
    {
        return (ULONG64)-1;
    }
    if (m_bWrite)
    {
        auto request = StreamMessage::MakeGetSize(StreamMessage::StreamRequest::Write);

        Concurrency::send<StreamMessage::Message>(m_request, request);

        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            return 0LL;
        }
        return request->GetStreamSize();
    }
    return 0LL;
}

STDMETHODIMP ConcurrentStream::SetSize(ULONG64 ullNewSize)
{
    if (m_request == nullptr || m_result == nullptr)
        return E_NOT_VALID_STATE;

    if (m_bRead)
    {
        auto request = StreamMessage::MakeSetSize(StreamMessage::StreamRequest::Read, ullNewSize);

        Concurrency::send<StreamMessage::Message>(m_request, request);

        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            return result->HResult();
        }
    }
    if (m_bWrite)
    {
        auto request = StreamMessage::MakeSetSize(StreamMessage::StreamRequest::Write, ullNewSize);

        Concurrency::send<StreamMessage::Message>(m_request, request);

        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            return result->HResult();
        }
    }
    return S_OK;
}

STDMETHODIMP ConcurrentStream::Close()
{
    if (m_request == nullptr || m_result == nullptr)
        return E_NOT_VALID_STATE;

    if (m_bRead)
    {
        auto request = StreamMessage::MakeCloseRequest(StreamMessage::StreamRequest::Read);

        Concurrency::send<StreamMessage::Message>(m_request, request);
        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            m_request = nullptr;
            m_result = nullptr;
            return result->HResult();
        }
    }
    if (m_bWrite)
    {
        auto request = StreamMessage::MakeCloseRequest(StreamMessage::StreamRequest::Write);

        Concurrency::send<StreamMessage::Message>(m_request, request);
        auto result = Concurrency::receive<StreamMessage::Message>(m_result);
        if (FAILED(result->HResult()))
        {
            m_request = nullptr;
            m_result = nullptr;
            return result->HResult();
        }
    }
    m_request = nullptr;
    m_result = nullptr;
    return S_OK;
}
