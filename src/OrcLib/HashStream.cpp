//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "HashStream.h"

using namespace Orc;

HRESULT HashStream::Read_(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (m_bWriteOnly)
        return E_NOTIMPL;

    if (pReadBuffer == NULL)
        return E_INVALIDARG;
    if (cbBytes > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead == NULL)
        return E_POINTER;
    if (m_pChainedStream == nullptr)
        return E_POINTER;

    *pcbBytesRead = 0;

    if (m_pChainedStream->CanRead() != S_OK)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

    ULONGLONG cbBytesRead = 0L;
    if (FAILED(hr = m_pChainedStream->Read(pReadBuffer, cbBytes, &cbBytesRead)))
        return hr;

    if (cbBytesRead > 0)
    {
        if (FAILED(hr = HashData((LPBYTE)pReadBuffer, (DWORD)cbBytesRead)))
            return hr;
    }

    *pcbBytesRead = cbBytesRead;
    return S_OK;
}

HRESULT HashStream::Write_(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (pWriteBuffer == NULL)
        return E_INVALIDARG;
    *pcbBytesWritten = 0;

    if (cbBytesToWrite > MAXDWORD)
    {
        Log::Error("HashStream: Too many bytes to hash");
        return E_INVALIDARG;
    }

    if (FAILED(hr = HashData((LPBYTE)pWriteBuffer, (DWORD)cbBytesToWrite)))
        return hr;

    if (m_pChainedStream != nullptr && m_bWriteOnly)
    {
        ULONGLONG cbBytesWritten = 0;

        if (m_pChainedStream->CanWrite() != S_OK)
            return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

        if (FAILED(hr = m_pChainedStream->Write(pWriteBuffer, cbBytesToWrite, &cbBytesWritten)))
            return hr;

        *pcbBytesWritten = cbBytesWritten;
    }
    else
        *pcbBytesWritten = cbBytesToWrite;

    return S_OK;
}

HRESULT
HashStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    HRESULT hr = E_FAIL;

    if (m_pChainedStream == nullptr)
        return E_POINTER;

    if (FAILED(hr = m_pChainedStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer)))
        return hr;

    if (FAILED(hr = ResetHash(DistanceToMove == 0LL && dwMoveMethod == FILE_BEGIN)))
        return hr;

    return S_OK;
}

HRESULT HashStream::Close()
{
    if (m_pChainedStream == nullptr)
        return S_OK;
    return m_pChainedStream->Close();
}

HashStream::~HashStream() {}
