//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "CacheStream.h"

using namespace std::string_view_literals;

namespace Orc {

CacheStream::CacheStream(std::shared_ptr<ByteStream> stream)
    : ByteStream()
    , m_stream(std::move(stream))
    , m_streamOffset(0)
    , m_cache()
    , m_cacheSize(0)
    , m_offset(0)
    , m_cacheLoadOffset(0)
{
}

STDMETHODIMP Orc::CacheStream::Clone(std::shared_ptr<ByteStream>& clone)
{
    return E_NOTIMPL;
}

HRESULT CacheStream::Close()
{
    m_stream->Close();
    return S_OK;
}

HRESULT CacheStream::Duplicate(const CacheStream& other)
{
    m_stream = other.m_stream;
    return S_OK;
}

HRESULT CacheStream::Open()
{
    HRESULT hr = m_stream->IsOpen();
    if (hr != S_OK)
    {
        Log::Debug("Failed to open CacheStream: underlying stream is closed [{}]", SystemError(hr));
        return E_FAIL;
    }

    if (m_stream->CanSeek() == S_OK)
    {
        ULONGLONG currPointer;
        hr = m_stream->SetFilePointer(0, FILE_BEGIN, &currPointer);
        if (FAILED(hr))
        {
            Log::Debug("Failed to open CacheStream: underlying stream seek failed [{}]", SystemError(hr));
            return E_FAIL;
        }
    }
    else
    {
        Log::Trace("CacheStream cannot seek underlying stream [{}]", SystemError(hr));
    }

    ULONGLONG written = 0;
    hr = m_stream->Read(m_cache.data(), m_cache.size(), &written);
    if (FAILED(hr))
    {
        Log::Debug("Failed to open CacheStream: underlying stream failed to copy [{}]", SystemError(hr));
        return hr;
    }

    m_streamOffset += written;
    m_offset = 0;
    m_cacheLoadOffset = 0;
    m_cacheSize = written;
    return S_OK;
}

HRESULT CacheStream::Read_(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (m_cacheSize == 0)
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
        if (m_offset >= m_cacheLoadOffset && m_offset < m_cacheLoadOffset + m_cacheSize)
        {
            const auto offset = m_offset - m_cacheLoadOffset;

            ULONGLONG cacheRead = std::min(cbBytes - totalRead, m_cacheSize - offset);
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
            hr = m_stream->Read(m_cache.data(), m_cache.size(), &streamRead);
            if (FAILED(hr))
            {
                return hr;
            }

            if (streamRead == 0)
            {
                break;
            }

            m_cacheLoadOffset = m_streamOffset;
            m_cacheSize = streamRead;
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
    HRESULT hr = m_stream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
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
    return m_stream->GetSize();
}

HRESULT CacheStream::SetSize(ULONG64 ullNewSize)
{
    return E_NOTIMPL;
}

CacheStream::~CacheStream() {}

}  // namespace Orc
