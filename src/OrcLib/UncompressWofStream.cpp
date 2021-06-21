//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2021 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "UncompressWofStream.h"

#include "ByteStream.h"
#include "NTFSCompression.h"
#include "NTFSStream.h"
#include "Stream/StreamConcept.h"
#include "Stream/ByteStreamConcept.h"
#include "Utils/BufferView.h"
#include "VolumeReader.h"

using namespace Orc::Ntfs;
using namespace Orc;

namespace Orc {

UncompressWofStream::UncompressWofStream()
    : ChainingStream()
    , m_buffer()
{
}

UncompressWofStream::~UncompressWofStream() {}

HRESULT UncompressWofStream::Close()
{
    if (m_pChainedStream == nullptr)
    {
        return S_OK;
    }

    return m_pChainedStream->Close();
}

HRESULT UncompressWofStream::Open(const std::shared_ptr<ByteStream>& pChained)
{
    if (pChained == NULL)
    {
        return E_POINTER;
    }

    if (pChained->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream must be opened");
        return E_FAIL;
    }

    m_pChainedStream = pChained;
    return S_OK;
}

HRESULT UncompressWofStream::Open(
    const std::shared_ptr<NTFSStream>& ntfsStream,
    WofAlgorithm algorithm,
    uint64_t uncompressedSize)
{
    std::error_code ec;
    auto decompressor = NtDecompressorConcept(algorithm, ec);
    if (ec)
    {
        Log::Debug("Failed to create decompressor [{}]", ec);
        return E_FAIL;
    }

    HRESULT hr = Open(ntfsStream);
    if (FAILED(hr))
    {
        return hr;
    }

    m_wofStream = WofStreamConcept(
        std::move(decompressor),
        ByteStreamConcept(ntfsStream),
        0,
        algorithm,
        ntfsStream->GetSize(),
        uncompressedSize,
        ec);
    if (ec)
    {
        Log::Debug("Failed to create wof stream [{}]", ec);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT UncompressWofStream::Read(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;
    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead != nullptr)
        *pcbBytesRead = 0;

    std::error_code ec;
    gsl::span<uint8_t> output(reinterpret_cast<uint8_t*>(pBuffer), cbBytesToRead);
    const auto processed = m_wofStream.Read(output, ec);
    if (ec)
    {
        Log::Debug("Failed to read ntfs stream [{}]", ec);
        return E_FAIL;
    }

    if (pcbBytesRead)
    {
        *pcbBytesRead = processed;
    }

    return S_OK;
}

HRESULT UncompressWofStream::Write(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesWritten)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBytes);
    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);

    return E_NOTIMPL;
}

HRESULT UncompressWofStream::SetFilePointer(
    __in LONGLONG lDistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pqwCurrPointer)
{
    if (!m_pChainedStream)
        return E_FAIL;

    std::error_code ec;
    // TODO: ToSeekDirection()
    // const auto offset = m_buffer.Seek(static_cast<SeekDirection>(dwMoveMethod), lDistanceToMove, ec);
    const auto offset = m_wofStream.Seek(static_cast<SeekDirection>(dwMoveMethod), lDistanceToMove, ec);
    if (ec)
    {
        Log::Debug("Failed to seek ntfs stream [{}]", ec);
        return E_FAIL;
    }

    if (pqwCurrPointer)
    {
        *pqwCurrPointer = offset;
    }

    return S_OK;
}

ULONG64 UncompressWofStream::GetSize()
{
    return m_wofStream.UncompressedSize();
}

HRESULT UncompressWofStream::SetSize(ULONG64 ullNewSize)
{
    DBG_UNREFERENCED_PARAMETER(ullNewSize);
    return S_OK;
}

}  // namespace Orc
