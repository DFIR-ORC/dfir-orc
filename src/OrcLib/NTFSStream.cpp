//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "NTFSStream.h"

#include "Temporary.h"

#include "LogFileWriter.h"

#include "VolumeReader.h"
#include "MFTRecord.h"
#include "MFTUtils.h"

using namespace Orc;

NTFSStream::NTFSStream(logger pLog)
    : ByteStream(std::move(pLog))
{
    m_pVolReader = NULL;
    m_CurrentPosition = 0LL;
    m_DataSize = 0LL;
    m_CurrentSegmentOffset = 0LL;
    m_CurrentSegmentIndex = 0;
    m_bAllocatedData = false;
}

/*
    NTFSStream::Close

    Frees all resources and resets object to invalid state
*/
HRESULT NTFSStream::Close()
{
    m_pVolReader.reset();
    m_DataSegments.clear();
    m_CurrentPosition = 0;
    m_DataSize = 0;
    m_CurrentSegmentOffset = 0LL;
    m_CurrentSegmentIndex = 0;
    m_bAllocatedData = false;
    return S_OK;
}

HRESULT NTFSStream::OpenStream(
    const std::shared_ptr<VolumeReader>& pReader,
    const std::shared_ptr<MftRecordAttribute>& pDataAttr)
{
    HRESULT hr = E_FAIL;

    Close();

    _ASSERT(pReader);

    m_pVolReader = pReader;

    auto pNRInfo = pDataAttr->GetNonResidentInformation(pReader);

    if (pNRInfo != nullptr)
    {
        if (FAILED(hr = MFTUtils::GetDataSegments(*pNRInfo, m_DataSegments)))
            return hr;

        if (FAILED(hr = pDataAttr->DataSize(m_pVolReader, m_DataSize)))
            return hr;
    }
    return S_OK;
}

HRESULT NTFSStream::OpenAllocatedDataStream(
    const std::shared_ptr<VolumeReader>& pReader,
    const std::shared_ptr<MftRecordAttribute>& pDataAttr)
{
    HRESULT hr = OpenStream(pReader, pDataAttr);

    m_bAllocatedData = true;

    return hr;
}

/*
    NTFSStream::Read

    Reads data from the stream

    Parameters:
        pReadBuffer     -   Pointer to buffer which receives the data
        cbBytes         -   Number of bytes to read from stream
        pcbBytesRead    -   Recieves the number of bytes copied to pReadBuffer
*/
HRESULT NTFSStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pullBytesRead)
{
    HRESULT hr = E_FAIL;

    if (pullBytesRead != nullptr)
        *pullBytesRead = 0LL;

    if (cbBytes == 0LL)
        return S_OK;

    if (m_CurrentSegmentIndex >= m_DataSegments.size())
        return S_OK;

    if (m_CurrentSegmentOffset
        >= (m_bAllocatedData ? m_DataSegments[m_CurrentSegmentIndex].ullAllocatedSize
                             : m_DataSegments[m_CurrentSegmentIndex].ullSize))
    {
        m_CurrentSegmentIndex++;  // We have reached the end of the segment, moving to the next
        m_CurrentSegmentOffset = 0LL;
    }

    if (m_CurrentSegmentIndex >= m_DataSegments.size())
        return S_OK;

    ULONGLONG ullToRead = std::min(
        cbBytes,
        (m_bAllocatedData ? m_DataSegments[m_CurrentSegmentIndex].ullAllocatedSize
                          : m_DataSegments[m_CurrentSegmentIndex].ullSize)
            - m_CurrentSegmentOffset);

    CBinaryBuffer buffer((PBYTE)pReadBuffer, static_cast<size_t>(cbBytes));
    ULONGLONG ullBytesRead = 0LL;

    if (m_DataSegments[m_CurrentSegmentIndex].bUnallocated || !m_DataSegments[m_CurrentSegmentIndex].bValidData)
    {
        ZeroMemory(buffer.GetData(), static_cast<size_t>(ullToRead));
        ullBytesRead = ullToRead;
    }
    else
    {
        if (FAILED(
                hr = m_pVolReader->Read(
                    m_DataSegments[m_CurrentSegmentIndex].ullDiskBasedOffset + m_CurrentSegmentOffset,
                    buffer,
                    ullToRead,
                    ullBytesRead)))
            return hr;
    }
    m_CurrentSegmentOffset += ullBytesRead;
    m_CurrentPosition += ullBytesRead;
    if (pullBytesRead != nullptr)
        *pullBytesRead = ullBytesRead;
    return S_OK;
}

/*
    NTFSStream::Write

    Parameters:
        pWriteBuffer    -   Pointer to buffer to write from
        cbBytesToWrite  -   Number of bytes to write to the stream
        pcbBytesWritten -   Recieves the number of written to the stream
*/
HRESULT NTFSStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);
    DBG_UNREFERENCED_PARAMETER(pWriteBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBytesToWrite);
    return E_NOTIMPL;
}

/*
    NTFSStream::SetFilePointer

    Sets the stream's current pointer. If the resultant pointer is greater
    than the number of valid bytes in the buffer, the resultant is clipped
    to the latter value

    Parameters:
        See SetFilePointer() doc for the descriptions of all parameters
*/
HRESULT
NTFSStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    ULONGLONG ullNewFilePointer = 0;

    switch (dwMoveMethod)
    {
        case FILE_CURRENT:
            ullNewFilePointer = m_CurrentPosition + DistanceToMove;

            if (ullNewFilePointer > m_DataSize)
                return E_INVALIDARG;

            if (m_CurrentSegmentIndex >= m_DataSegments.size())
            {
                m_CurrentSegmentIndex--;
            }

            if (DistanceToMove > 0)
            {
                for (size_t i = m_CurrentSegmentIndex; i < m_DataSegments.size(); i++)
                {
                    if (ullNewFilePointer >= m_DataSegments[i].ullFileBasedOffset
                        && ullNewFilePointer < (m_DataSegments[i].ullFileBasedOffset + m_DataSegments[i].ullSize))
                    {
                        m_CurrentSegmentIndex = i;
                        m_CurrentPosition = ullNewFilePointer;
                        m_CurrentSegmentOffset = ullNewFilePointer - m_DataSegments[i].ullFileBasedOffset;
                        break;
                    }
                }
            }
            else if (DistanceToMove < 0)
            {
                for (int32_t i = static_cast<int32_t>(m_CurrentSegmentIndex); i > 0; i--)
                {
                    if (ullNewFilePointer >= m_DataSegments[i].ullFileBasedOffset
                        && ullNewFilePointer < (m_DataSegments[i].ullFileBasedOffset + m_DataSegments[i].ullSize))
                    {
                        m_CurrentSegmentIndex = i;
                        m_CurrentPosition = ullNewFilePointer;
                        m_CurrentSegmentOffset = ullNewFilePointer - m_DataSegments[i].ullFileBasedOffset;
                        break;
                    }
                }
            }

            break;
        case FILE_END:
            if (DistanceToMove > 0)
            {
                log::Error(_L_, E_INVALIDARG, L"Cannot move past the end of the file (%I64d)\r\n", DistanceToMove);
                return E_INVALIDARG;
            }

            ullNewFilePointer = m_DataSize + DistanceToMove;
            {
                ULONGLONG ullCurrentPosition = m_DataSize;
                m_CurrentSegmentIndex = m_DataSegments.size() - 1;
                for (auto iter = m_DataSegments.crbegin(); iter != m_DataSegments.crend(); ++iter)
                {
                    if (ullNewFilePointer <= ullCurrentPosition
                        && ullNewFilePointer >= ullCurrentPosition - iter->ullSize)
                    {
                        // This is the one segment
                        m_CurrentSegmentOffset = ullCurrentPosition - ullNewFilePointer;
                        break;
                    }
                    ullCurrentPosition -= m_DataSegments[m_CurrentSegmentIndex].ullSize;
                    m_CurrentSegmentIndex--;
                }
            }
            break;
        case FILE_BEGIN:
            ullNewFilePointer = DistanceToMove;
            m_CurrentSegmentOffset = 0LL;
            m_CurrentSegmentIndex = 0LL;

            if (ullNewFilePointer > 0LL && ullNewFilePointer <= m_DataSize)
            {
                ULONGLONG ullCurrentPosition = 0LL;
                for (auto iter = begin(m_DataSegments); iter != end(m_DataSegments); ++iter)
                {
                    if (ullNewFilePointer >= ullCurrentPosition
                        && ullNewFilePointer <= ullCurrentPosition + iter->ullSize)
                    {
                        // This is the one segment
                        m_CurrentSegmentOffset = ullNewFilePointer - ullCurrentPosition;
                        break;
                    }
                    ullCurrentPosition += m_DataSegments[m_CurrentSegmentIndex].ullSize;
                    m_CurrentSegmentIndex++;
                }
            }
            else if (ullNewFilePointer > m_DataSize && !m_DataSegments.empty())
            {
                m_CurrentPosition = m_DataSize;
                m_CurrentSegmentIndex = m_DataSegments.size() - 1;
            }
            else
            {
                m_CurrentPosition = 0LL;
                if (pCurrPointer)
                    *pCurrPointer = m_CurrentPosition;
                return S_OK;
            }
            break;
        default:
            return E_INVALIDARG;
    }

    //
    // Clip the pointer to [0, datasize]
    //
    m_CurrentPosition = std::min(m_DataSize, ullNewFilePointer);
    if (pCurrPointer)
        *pCurrPointer = m_CurrentPosition;
    return S_OK;
}

//
// Destructor
//
NTFSStream::~NTFSStream()
{
    Close();
}
