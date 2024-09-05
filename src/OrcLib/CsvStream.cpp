//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "CsvStream.h"

using namespace Orc;

STDMETHODIMP Orc::TableOutput::CSV::Stream::Initialize(
    bool bfirstRowIsColumnNames,
    WCHAR wcSeparator,
    WCHAR wcQuote,
    const WCHAR* szDateFormat,
    DWORD dwSkipLines)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Cruncher.Initialize(bfirstRowIsColumnNames, wcSeparator, wcQuote, szDateFormat, dwSkipLines)))
    {
        return hr;
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Stream::Write_(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);

    CBinaryBuffer data((LPBYTE)pBuffer, (size_t)cbBytes);

    if (FAILED(hr = m_Cruncher.AddData(data)))
        return hr;

    if (!m_bReady && m_Cruncher.GetAvailableSize() >= CSV_READ_CHUNK_IN_BYTES)
    {
        if (FAILED(hr = m_Cruncher.PeekHeaders()))
        {
        }
    }
    return S_OK;
}

Orc::TableOutput::CSV::Stream::~Stream() {}
