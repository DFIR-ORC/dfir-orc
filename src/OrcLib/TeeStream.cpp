//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "TeeStream.h"

using namespace Orc;

HRESULT TeeStream::Close()
{
    auto hr = S_OK;

    for (const auto& stream : m_Streams)
    {
        auto stream_hr = E_FAIL;
        if (stream && FAILED(stream_hr = stream->Close()))
            hr = stream_hr;
    }

    return hr;
}

HRESULT TeeStream::Open(std::vector<std::shared_ptr<ByteStream>>&& Streams)
{
    std::swap(m_Streams, Streams);
    return S_OK;
}

HRESULT TeeStream::Read_(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead)
        *pcbBytesRead = 0;
    return E_NOTIMPL;
}

HRESULT
TeeStream::Write_(__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten)
{
    if (cbBytes > MAXDWORD)
        return E_INVALIDARG;

    HRESULT hr = S_OK;

    for (const auto& stream : m_Streams)
    {
        auto stream_hr = E_FAIL;
        if (stream && FAILED(stream_hr = stream->Write(pBuffer, cbBytes, pcbBytesWritten)))
            hr = stream_hr;
    }

    if (pcbBytesWritten)
        *pcbBytesWritten = cbBytes;
    return hr;
}

HRESULT
TeeStream::SetFilePointer(__in LONGLONG lDistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pqwCurrPointer)
{
    if (pqwCurrPointer)
    {
        // as Tee is setting two streams position it cannot set correctly 'pqwCurrentPointer'
        *pqwCurrPointer = -1;
    }

    HRESULT hr = S_OK;
    for (const auto& stream : m_Streams)
    {
        auto stream_hr = E_FAIL;
        if (stream && FAILED(stream_hr = stream->SetFilePointer(lDistanceToMove, dwMoveMethod, nullptr)))
            hr = stream_hr;
    }
    return hr;
}

ULONG64 TeeStream::GetSize()
{
    LARGE_INTEGER Size = {0};
    return Size.QuadPart;
}

HRESULT TeeStream::SetSize(ULONG64 ullNewSize)
{
    HRESULT hr = S_OK;
    for (const auto& stream : m_Streams)
    {
        auto stream_hr = E_FAIL;
        if (stream && FAILED(stream_hr = stream->SetSize(ullNewSize)))
            hr = stream_hr;
    }
    return hr;
}

TeeStream::~TeeStream() {}
