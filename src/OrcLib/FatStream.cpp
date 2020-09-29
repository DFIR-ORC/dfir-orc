//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "Stdafx.h"

#include "FatStream.h"
#include "FatFileEntry.h"
#include "VolumeReader.h"

#include <algorithm>

using namespace Orc;

FatStream::FatStream(const std::shared_ptr<VolumeReader>& pVolReader, const std::shared_ptr<FatFileEntry>& fileEntry)
    : ByteStream()
    , m_pVolReader(pVolReader)
    , m_FatFileEntry(fileEntry)
    , m_CurrentPosition(0LL)
{
    if (m_FatFileEntry != nullptr)
    {
        const SegmentDetailsMap& map(m_FatFileEntry->m_SegmentDetailsMap);
        m_CurrentSegmentDetails = map.begin();
    }
}

FatStream::~FatStream()
{
    Close();
}

HRESULT FatStream::IsOpen()
{
    if (m_pVolReader == nullptr || m_FatFileEntry == nullptr)
        return E_FAIL;

    if (GetSize() > 0LL && !m_FatFileEntry->m_SegmentDetailsMap.empty())
    {
        return S_OK;
    }

    return S_FALSE;
};

ULONG64 FatStream::GetSize()
{
    if (m_FatFileEntry == nullptr)
        return 0LL;

    return m_FatFileEntry->GetSize();
}

HRESULT FatStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pullBytesRead)
{
    HRESULT hr = E_FAIL;

    if (m_pVolReader == nullptr || m_FatFileEntry == nullptr)
        return hr;

    _ASSERT(pullBytesRead != nullptr);
    *pullBytesRead = 0LL;

    if (cbBytes == 0LL)
        return S_OK;

    const SegmentDetailsMap& map(m_FatFileEntry->m_SegmentDetailsMap);

    if (m_CurrentSegmentDetails == map.end())
        return S_OK;

    ULONGLONG ullRemainingSegmentSize = m_CurrentSegmentDetails->second.mSize;

    if (m_CurrentPosition > m_CurrentSegmentDetails->first)
    {
        ULONGLONG ulDelta = m_CurrentPosition - m_CurrentSegmentDetails->first;

        if (ullRemainingSegmentSize > ulDelta)
        {
            ullRemainingSegmentSize -= ulDelta;
        }
        else
        {
            // should not happen
            return hr;
        }
    }

    ULONGLONG ullToRead = std::min(cbBytes, ullRemainingSegmentSize);

    CBinaryBuffer buffer((PBYTE)pReadBuffer, static_cast<size_t>(ullToRead));
    ULONGLONG ullBytesRead = 0LL;
    ULONGLONG ullOffset = m_CurrentSegmentDetails->second.mStartOffset;

    if (m_CurrentPosition > m_CurrentSegmentDetails->first)
    {
        ullOffset += m_CurrentPosition - m_CurrentSegmentDetails->first;
    }

    if (FAILED(hr = m_pVolReader->Read(ullOffset, buffer, ullToRead, ullBytesRead)))
        return hr;

    m_CurrentPosition += ullBytesRead;

    if (m_CurrentPosition >= m_CurrentSegmentDetails->first + m_CurrentSegmentDetails->second.mSize)
    {
        m_CurrentSegmentDetails++;
    }

    *pullBytesRead = ullBytesRead;

    return S_OK;
}

HRESULT FatStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);
    DBG_UNREFERENCED_PARAMETER(pWriteBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBytesToWrite);
    return E_NOTIMPL;
}

HRESULT
FatStream::SetFilePointer(__in LONGLONG distanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    if (m_FatFileEntry == nullptr)
        return E_FAIL;

    ULONGLONG ullNewFilePointer = 0;
    const SegmentDetailsMap& map(m_FatFileEntry->m_SegmentDetailsMap);
    SegmentDetailsMap::const_iterator begin = map.begin();
    SegmentDetailsMap::const_iterator end = map.end();

    if (begin == end || GetSize() == 0)
        return E_FAIL;

    switch (dwMoveMethod)
    {
        case FILE_CURRENT:
            ullNewFilePointer = m_CurrentPosition + distanceToMove;

            if (ullNewFilePointer > GetSize())
            {
                m_CurrentPosition = GetSize();
                m_CurrentSegmentDetails = end;
            }
            else
            {
                // find the right segment
                if (distanceToMove > 0)
                {
                    for (SegmentDetailsMap::const_iterator it = m_CurrentSegmentDetails; it != end; it++)
                    {
                        const SegmentDetails& segmentDetails(it->second);

                        if (it->first <= ullNewFilePointer && ullNewFilePointer < it->first + segmentDetails.mSize)
                        {
                            m_CurrentPosition = ullNewFilePointer;
                            m_CurrentSegmentDetails = it;
                            break;
                        }
                    }
                }
                else if (distanceToMove < 0)
                {
                    for (SegmentDetailsMap::const_iterator it = m_CurrentSegmentDetails; it != begin; it--)
                    {
                        if (it != end)
                        {
                            const SegmentDetails& segmentDetails(it->second);

                            if (it->first <= ullNewFilePointer && ullNewFilePointer < it->first + segmentDetails.mSize)
                            {
                                m_CurrentPosition = ullNewFilePointer;
                                m_CurrentSegmentDetails = it;
                                break;
                            }
                        }
                    }
                }
            }

            break;

        case FILE_END:
            if (distanceToMove > 0)
            {
                spdlog::error("Cannot move past the end of the file ({})", distanceToMove);
                return E_INVALIDARG;
            }

            ullNewFilePointer = GetSize() + distanceToMove;
            m_CurrentSegmentDetails = end;

            for (SegmentDetailsMap::const_iterator it = m_CurrentSegmentDetails; it != begin; it--)
            {
                if (it != end)
                {
                    const SegmentDetails& segmentDetails(it->second);

                    if (it->first <= ullNewFilePointer && ullNewFilePointer < it->first + segmentDetails.mSize)
                    {
                        m_CurrentPosition = ullNewFilePointer;
                        m_CurrentSegmentDetails = it;
                        break;
                    }
                }
            }

            break;

        case FILE_BEGIN:
            if (distanceToMove < 0)
            {
                spdlog::error("Cannot move before the beginning of the file ({})", distanceToMove);
                return E_INVALIDARG;
            }

            ullNewFilePointer = distanceToMove;
            m_CurrentSegmentDetails = begin;

            if (ullNewFilePointer > 0LL)
            {
                if (ullNewFilePointer <= GetSize())
                {
                    for (SegmentDetailsMap::const_iterator it = begin; it != end; it++)
                    {
                        const SegmentDetails& segmentDetails(it->second);

                        if (it->first <= ullNewFilePointer && ullNewFilePointer < it->first + segmentDetails.mSize)
                        {
                            m_CurrentPosition = ullNewFilePointer;
                            m_CurrentSegmentDetails = it;
                            break;
                        }
                    }
                }
                else
                {
                    m_CurrentPosition = GetSize();
                    m_CurrentSegmentDetails = end;
                }
            }
            else
            {
                m_CurrentPosition = 0LL;
            }

            break;

        default:
            return E_INVALIDARG;
    }

    m_CurrentPosition = std::min(GetSize(), m_CurrentPosition);

    if (pCurrPointer)
        *pCurrPointer = m_CurrentPosition;

    return S_OK;
}

HRESULT FatStream::Close()
{
    m_CurrentPosition = 0LL;
    return S_OK;
}
