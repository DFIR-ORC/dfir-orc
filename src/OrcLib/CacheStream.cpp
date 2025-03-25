//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "CacheStream.h"

using namespace std::string_view_literals;

namespace Orc {

CacheStream::CacheStream(ByteStream& stream, size_t cacheSize)
    : ByteStream()
    , m_stream(stream)
    , m_streamOffset(0)
    , m_offset(0)
    , m_cache()
    , m_cacheUse(0)
    , m_cacheSize(cacheSize)
    , m_cacheOffset(0)
{
    m_cache.resize(cacheSize);
}

STDMETHODIMP Orc::CacheStream::Clone(ByteStream& clone)
{
    return E_NOTIMPL;
}

HRESULT CacheStream::Close()
{
    HRESULT hr = m_stream.Close();
    if (FAILED(hr))
    {
        Log::Debug("Failed to close underlying cached stream [{}]", SystemError(hr));
    }

    m_cache.resize(0);
    m_cache.shrink_to_fit();
    m_cacheUse = 0;
    return S_OK;
}

HRESULT CacheStream::Duplicate(const CacheStream& other)
{
    m_stream = other.m_stream;
    m_streamOffset = other.m_streamOffset;
    m_cacheOffset = other.m_cacheOffset;
    m_cacheUse = other.m_cacheUse;
    m_cacheSize = other.m_cacheSize;
    m_cache = other.m_cache;
    return S_OK;
}

HRESULT CacheStream::Open()
{
    HRESULT hr = m_stream.IsOpen();
    if (hr != S_OK)
    {
        Log::Debug("Failed to open CacheStream: underlying stream is closed [{}]", SystemError(hr));
        return E_FAIL;
    }

    m_cache.resize(m_cacheSize);
    m_cacheUse = 0;
    return S_OK;
}

HRESULT CacheStream::Read_(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    // Lazy allocation
    if (m_cache.size() == 0)
    {
        hr = Open();
        if (FAILED(hr))
        {
            return hr;
        }
    }

    ULONGLONG totalRead = 0;
    while (totalRead != cbBytes)
    {
        if (m_offset >= m_cacheOffset && m_offset < m_cacheOffset + m_cacheUse)
        {
            const auto offset = m_offset - m_cacheOffset;

            ULONGLONG cacheRead = std::min(cbBytes - totalRead, m_cacheUse - offset);
            std::copy(
                std::cbegin(m_cache) + offset,
                std::cbegin(m_cache) + offset + cacheRead,
                static_cast<uint8_t*>(pReadBuffer) + totalRead);

            m_offset += cacheRead;
            totalRead += cacheRead;
        }
        else
        {
            ULONGLONG streamRead = 0;
            hr = m_stream.Read(m_cache.data(), m_cache.size(), &streamRead);
            if (FAILED(hr))
            {
                return hr;
            }

            if (streamRead == 0)
            {
                break;
            }

            m_cacheOffset = m_streamOffset;
            m_cacheUse = streamRead;
            m_streamOffset += streamRead;
        }
    }

    if (pcbBytesRead)
    {
        *pcbBytesRead = totalRead;
    }

    return S_OK;
}

HRESULT CacheStream::Write_(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    return E_NOTIMPL;
}

HRESULT
CacheStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    ULONG64 newOffset = 0;
    if (!pCurrPointer)
    {
        pCurrPointer = &newOffset;
    }

    if (dwMoveMethod == FILE_CURRENT)
    {
        if (DistanceToMove == 0)
        {
            *pCurrPointer = m_offset;
            return S_OK;
        }

        dwMoveMethod = FILE_BEGIN;
        DistanceToMove = m_offset + DistanceToMove;
    }

    HRESULT hr = m_stream.SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
    if (FAILED(hr))
    {
        return hr;
    }

    m_streamOffset = *pCurrPointer;
    m_offset = *pCurrPointer;
    return S_OK;
}

ULONG64 CacheStream::GetSize()
{
    return m_stream.GetSize();
}

HRESULT CacheStream::SetSize(ULONG64 ullNewSize)
{
    return E_NOTIMPL;
}

CacheStream::~CacheStream() {}

}  // namespace Orc
