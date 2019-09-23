//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "AccumulatingStream.h"

#include "LogFileWriter.h"

#include "TemporaryStream.h"

using namespace Orc;

STDMETHODIMP AccumulatingStream::Open(
    const std::shared_ptr<ByteStream>& pChainedStream,
    const std::wstring& strTempDir,
    DWORD dwMemThreshold,
    DWORD dwBlockSize)
{
    HRESULT hr = E_FAIL;
    if (!pChainedStream)
        return E_POINTER;

    if (pChainedStream->CanWrite() == S_FALSE)
    {
        log::Error(_L_, E_INVALIDARG, L"Chained stream not able to write cannot be used in accumulating stream\r\n");
        return E_INVALIDARG;
    }

    if (pChainedStream->IsOpen() != S_OK)
    {
        log::Error(_L_, E_INVALIDARG, L"Chained stream must be opened to be used in accumulating stream\r\n");
        return E_INVALIDARG;
    }

    m_pChainedStream = pChainedStream;

    m_dwBlockSize = dwBlockSize;
    m_pTempStream = std::make_shared<TemporaryStream>(_L_);

    if (FAILED(hr = m_pTempStream->Open(strTempDir, L"AccumulatingStream", dwMemThreshold)))
    {
        log::Error(_L_, hr, L"Failed to create temporary stream in a accumulating stream\r\n");
        return hr;
    }

    return S_OK;
}

STDMETHODIMP AccumulatingStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pcbBytesRead);
    DBG_UNREFERENCED_PARAMETER(cbBytes);
    DBG_UNREFERENCED_PARAMETER(pReadBuffer);
    log::Error(_L_, HRESULT_FROM_WIN32(ERROR_INVALID_OPERATION), L"Impossible to read from accumulating stream\r\n");
    return HRESULT_FROM_WIN32(ERROR_INVALID_OPERATION);
}

STDMETHODIMP AccumulatingStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    return m_pTempStream->Write(pWriteBuffer, cbBytesToWrite, pcbBytesWritten);
}

STDMETHODIMP AccumulatingStream::SetFilePointer(
    __in LONGLONG DistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pCurrPointer)
{
    return m_pTempStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
}

STDMETHODIMP_(ULONG64) AccumulatingStream::GetSize()
{
    return m_pTempStream->GetSize();
}
STDMETHODIMP AccumulatingStream::SetSize(ULONG64 ullSize)
{
    return m_pTempStream->SetSize(ullSize);
}

STDMETHODIMP AccumulatingStream::Close()
{
    HRESULT hr = E_FAIL;

    if (m_bClosed)
        return S_OK;

    if (!m_pTempStream)
        return E_POINTER;
    if (!m_pChainedStream)
        return E_POINTER;

    if (FAILED(hr = m_pTempStream->SetFilePointer(0LL, FILE_BEGIN, NULL)))
    {
        log::Error(_L_, hr, L"Failed to reset temporary stream in accumulating stream\r\n");
        return hr;
    }

    ULONGLONG ullBytesRead;

    CBinaryBuffer buffer;

    if (!buffer.SetCount(m_dwBlockSize))
        return E_OUTOFMEMORY;

    if (FAILED(hr = m_pTempStream->Read(buffer.GetData(), m_dwBlockSize, &ullBytesRead)))
    {
        log::Error(_L_, hr, L"Failed to read from temporary stream in accumulating stream\r\n");
        return hr;
    }

    while (ullBytesRead > 0LL)
    {
        ULONGLONG ullBytesWritten = 0LL;
        if (FAILED(hr = m_pChainedStream->Write(buffer.GetData(), ullBytesRead, &ullBytesWritten)))
        {
            log::Error(_L_, hr, L"Failed to read from temporary stream in accumulating stream\r\n");
            return hr;
        }

        if (ullBytesRead != ullBytesWritten)
        {
            log::Warning(_L_, E_UNEXPECTED, L"Failed to read from temporary stream in accumulating stream\r\n");
        }

        if (FAILED(hr = m_pTempStream->Read(buffer.GetData(), m_dwBlockSize, &ullBytesRead)))
        {
            log::Error(_L_, hr, L"Failed to read from temporary stream in accumulating stream\r\n");
            return hr;
        }
    }

    m_pTempStream->Close();
    m_pChainedStream->Close();
    m_bClosed = true;
    return S_OK;
}

AccumulatingStream::~AccumulatingStream()
{
    if (!m_bClosed)
        Close();
}
