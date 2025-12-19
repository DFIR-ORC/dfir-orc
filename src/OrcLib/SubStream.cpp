//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "SubStream.h"

namespace Orc {

SubStream::SubStream(ByteStream& stream, uint64_t chunkOffset, uint64_t chunkSize)
    : ByteStream()
    , m_stream(stream)
    , m_chunkOffset(chunkOffset)
    , m_chunkSize(chunkSize)
{
    if (m_chunkOffset > m_stream.GetSize())
    {
        Log::Error("Invalid SubStream chunk offset (value: {}, stream size: {})", m_chunkOffset, m_stream.GetSize());
        return;
    }

    const auto maxChunkSize = m_stream.GetSize() - m_chunkOffset;
    if (chunkSize > maxChunkSize)
    {
        Log::Debug(
            "Requested chunk size exceeds stream bounds (chunk size: {}, max size: {})", chunkSize, maxChunkSize);
        chunkSize = maxChunkSize;
    }

    auto hr = m_stream.SetFilePointer(m_chunkOffset, FILE_BEGIN, nullptr);
    if (FAILED(hr))
    {
        Log::Error("Failed SetFilePointer [{}]", SystemError(hr));
        m_stream.Close();
    }
}

SubStream::~SubStream() {}

STDMETHODIMP Orc::SubStream::Clone(ByteStream& clone)
{
    return E_NOTIMPL;
}

HRESULT SubStream::Close()
{
    HRESULT hr = m_stream.Close();
    if (FAILED(hr))
    {
        Log::Debug("Failed to close underlying stream [{}]", SystemError(hr));
    }

    return S_OK;
}

HRESULT SubStream::Open()
{
    HRESULT hr = m_stream.IsOpen();
    if (hr != S_OK)
    {
        Log::Debug("Failed to open ChunkStream: underlying stream is closed [{}]", SystemError(hr));
        return E_FAIL;
    }

    return S_OK;
}

HRESULT SubStream::Read_(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    ULONG64 pos = 0;
    HRESULT hr = m_stream.SetFilePointer(0, FILE_CURRENT, &pos);
    if (FAILED(hr))
    {
        return hr;
    }

    const auto chunkPos = pos - m_chunkOffset;
    size_t length = std::min(cbBytes, m_chunkSize - chunkPos);
    return m_stream.Read(pReadBuffer, length, pcbBytesRead);
}

HRESULT SubStream::Write_(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    return E_NOTIMPL;
}

HRESULT
SubStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    ULONG64 streamPos = 0;
    HRESULT hr = m_stream.SetFilePointer(0, FILE_CURRENT, &streamPos);
    if (FAILED(hr))
    {
        return hr;
    }

    int64_t pos = 0;

    if (dwMoveMethod == FILE_BEGIN)
    {
        pos = m_chunkOffset + DistanceToMove;
    }
    else if (dwMoveMethod == FILE_CURRENT)
    {
        pos = streamPos + DistanceToMove;
    }
    else if (dwMoveMethod == FILE_END)
    {
        pos = m_chunkOffset + m_chunkSize + DistanceToMove;
    }
    else
    {
        return E_FAIL;
    }

    if (pos < m_chunkOffset && pos >= m_chunkOffset + m_chunkSize)
    {
        Log::Debug(
            "Failed SetFilePointer on chunk (offset: {:#x}, chunk offset: {:#x}, chunk size: {})",
            pos,
            m_chunkOffset,
            m_chunkSize);
        return E_FAIL;
    }

    hr = m_stream.SetFilePointer(pos, dwMoveMethod, &streamPos);
    if (FAILED(hr))
    {
        Log::Debug(
            "Failed SetFilePointer on underlying stream (offset: {:#x}, chunk offset: {:#x}, chunk size: {})",
            pos,
            m_chunkOffset,
            m_chunkSize);
        return hr;
    }

    if (pCurrPointer)
    {
        *pCurrPointer = streamPos - m_chunkOffset;
    }

    return S_OK;
}

ULONG64 SubStream::GetSize()
{
    return m_chunkSize;
}

HRESULT SubStream::SetSize(ULONG64 ullNewSize)
{
    return E_NOTIMPL;
}

}  // namespace Orc
