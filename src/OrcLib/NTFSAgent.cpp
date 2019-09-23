//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "NTFSAgent.h"

#include "VolumeReader.h"
#include "MFTRecord.h"

HRESULT NTFSAgent::OpenStream(
    __in_opt const std::shared_ptr<VolumeReader>& pVolReader,
    __in_opt MFTRecord* pRecord,
    __in_opt const std::shared_ptr<DataAttribute>& pDataAttr)
{
    HRESULT hr = E_FAIL;

    Close();

    m_pVolReader = pVolReader;
    m_pRecord = pRecord;
    m_pDataAttr = pDataAttr;

    m_bCanRead = true;
    m_bCanWrite = false;
    m_bCanSeek = true;
    return S_OK;
}

HRESULT NTFSAgent::Read(ULONGLONG ullBytesToRead, CBinaryBuffer& buffer)
{
    HRESULT hr = E_FAIL;

    ULONGLONG ullBytesRead = 0;

    ULONGLONG cbBytes = min(ullBytesToRead, GetSize());

    if (FAILED(hr = m_pRecord->ReadData(m_pVolReader, m_pDataAttr, m_CurrentPosition, cbBytes, buffer, &ullBytesRead)))
        return hr;

    buffer.SetCount((size_t)ullBytesRead);
    m_CurrentPosition += buffer.GetCount();

    return S_OK;
}

HRESULT NTFSAgent::Write(ULONGLONG cbBytesToWrite, CBinaryBuffer& buffer, ULONGLONG* pBytesWritten)
{
    HRESULT hr = E_FAIL;

    return S_OK;
}

ULONG64 NTFSAgent::GetSize()
{
    HRESULT hr = E_FAIL;

    if (m_DataSize == 0)
    {
        ULONGLONG datasize = 0;

        if (m_pVolReader == NULL || m_pDataAttr == NULL)
            return 0;

        if (FAILED(hr = m_pDataAttr->DataSize(m_pVolReader, datasize)))
            return 0;

        m_DataSize = datasize;
    }

    return m_DataSize;
}

HRESULT
NTFSAgent::SetFilePointer(__in LONGLONG lDistanceToMove, __in DWORD dwMoveMethod, __out_opt PLONGLONG pqwCurrPointer)
{
    HRESULT hr = E_FAIL;
    ULONGLONG ullNewFilePointer = 0;

    ULONGLONG datasize = GetSize();

    switch (dwMoveMethod)
    {
        case SEEK_CUR:
            ullNewFilePointer = m_CurrentPosition + lDistanceToMove;
            break;
        case SEEK_END:
            ullNewFilePointer = datasize + lDistanceToMove;
            break;
        case SEEK_SET:
            ullNewFilePointer = lDistanceToMove;
            break;
        default:
            return E_INVALIDARG;
    }

    //
    // Clip the pointer to [0, datasize]
    //
    if (m_CurrentPosition == min(datasize, ullNewFilePointer))
        return S_OK;

    m_CurrentPosition = min(datasize, ullNewFilePointer);
    return S_OK;
}

HRESULT NTFSAgent::Close()
{
    return S_OK;
}

NTFSAgent::~NTFSAgent(void)
{
    Close();
}
