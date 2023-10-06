//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2023 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "UncompressNTFSStream.h"

#include "NTFSCompression.h"
#include "VolumeReader.h"
#include "Utils/Round.h"
#include "Utils/BufferSpan.h"
#include "Stream/StreamUtils.h"
#include "Stream/VolumeStreamReader.h"

using namespace Orc;

UncompressNTFSStream::UncompressNTFSStream()
    : NTFSStream()
    , m_volume(nullptr)
    , m_ullPosition(0L)
    , m_dwCompressionUnit(0)
    , m_dwMaxCompressionUnit(0)
{
}

HRESULT UncompressNTFSStream::OpenAllocatedDataStream(
    const std::shared_ptr<VolumeReader>& pReader,
    const std::shared_ptr<MftRecordAttribute>& pDataAttr)
{
    HRESULT hr = NTFSStream::OpenAllocatedDataStream(pReader, pDataAttr);
    if (FAILED(hr))
    {
        return hr;
    }

    m_volume = std::make_unique<VolumeStreamReader>(m_pVolReader);

    DWORD dwCompressionUnit = 16 * m_pVolReader->GetBytesPerCluster();
    switch (dwCompressionUnit)
    {
        case 0x2000:
        case 0x4000:
        case 0x8000:
        case 0x10000:
            m_dwCompressionUnit = dwCompressionUnit;
            break;
        default:
            Log::Error(
                "Invalid Compression unit size specified {} (valid values are: 0x2000,0x4000,0x8000,0x10000)",
                dwCompressionUnit);
            return E_INVALIDARG;
    }

    m_dwCompressionUnit = dwCompressionUnit;
    m_dwMaxCompressionUnit = (DWORD)(GetSize() / m_dwCompressionUnit) + ((GetSize() % m_dwCompressionUnit) ? 1 : 0);

    const auto segments = DataSegments();

    std::vector<boost::logic::tribool> CUMethod;
    CUMethod.resize(m_dwMaxCompressionUnit);

    std::for_each(begin(CUMethod), end(CUMethod), [](boost::logic::tribool& aTriBool) {
        aTriBool.value = boost::logic::tribool::indeterminate_value;
    });

    {
        DWORD dwCurrentSegment = 0;
        DWORD dwInSegmentOffset = 0;

        for (unsigned int curCU = 0; curCU < m_dwMaxCompressionUnit; curCU++)
        {
            DWORD dwInCUOffset = 0;
            ULONGLONG ullSegmentSize = 0LL;

            const auto& currentSegment = segments.at(dwCurrentSegment);

            ullSegmentSize = currentSegment.ullAllocatedSize;

            if ((ullSegmentSize - dwInSegmentOffset) >= m_dwCompressionUnit)
            {
                CUMethod[curCU] = false;
                dwInSegmentOffset += m_dwCompressionUnit;
                if (dwInSegmentOffset >= ullSegmentSize)
                {
                    dwCurrentSegment++;
                    dwInSegmentOffset = 0L;
                }
            }
            else
            {
                bool bCUHasData = false;
                bool bCUHasSparse = false;

                DWORD dwThisCUInSegmentOffset = 0L;

                while (dwInCUOffset < m_dwCompressionUnit && dwCurrentSegment < segments.size())
                {
                    if (segments[dwCurrentSegment].bUnallocated)
                        bCUHasSparse = true;
                    else
                        bCUHasData = true;

                    DWORD dwAvailableChunk = ((DWORD)ullSegmentSize - dwInSegmentOffset);
                    DWORD dwChunk = 0L;

                    if (dwAvailableChunk > (m_dwCompressionUnit - dwInCUOffset))
                    {
                        dwChunk = m_dwCompressionUnit - dwInCUOffset;
                    }
                    else
                    {
                        dwChunk = dwAvailableChunk;
                    }

                    dwInCUOffset += dwChunk % m_dwCompressionUnit;
                    dwThisCUInSegmentOffset += dwChunk % m_dwCompressionUnit;

                    dwInSegmentOffset += dwThisCUInSegmentOffset;

                    if (dwInSegmentOffset >= ullSegmentSize)
                    {
                        dwCurrentSegment++;
                        dwInSegmentOffset = 0L;
                        dwThisCUInSegmentOffset = 0L;

                        if (dwCurrentSegment < segments.size())
                            ullSegmentSize = segments[dwCurrentSegment].ullAllocatedSize;
                    }
                }
                if (bCUHasData && bCUHasSparse)
                    CUMethod[curCU] = true;
            }
        }
    }

    std::swap(m_IsBlockCompressed, CUMethod);

    if (auto indeterCount =
            std::count_if(
                begin(m_IsBlockCompressed),
                end(m_IsBlockCompressed),
                [](const boost::logic::tribool& aTribool) { return boost::logic::indeterminate(aTribool); })
            > 0)
    {
        Log::Debug(
            "Failed to determine compression status of at least one CU ({} indeterminate out of {} compression units)",
            indeterCount,
            m_IsBlockCompressed.size());
        return S_FALSE;
    }

    return hr;
}

HRESULT UncompressNTFSStream::ReadCompressionUnit(
    DWORD dwNbCU,
    CBinaryBuffer& uncompressedData,
    __out_opt PULONGLONG pcbBytesRead)
{
    std::error_code ec;
    HRESULT hr = E_FAIL;

    if (pcbBytesRead)
        *pcbBytesRead = 0LL;

    if (dwNbCU > m_dwMaxCompressionUnit)
    {
        Log::Error(
            "Attempted to read past the last compression unit (tried {}, max is {})", dwNbCU, m_dwMaxCompressionUnit);
        return hr;
    }

    size_t uncomp_processed = 0;

    CBinaryBuffer buffer(true);
    for (size_t i = 0; i < dwNbCU; ++i)
    {
        hr = ReadRaw(buffer, m_dwCompressionUnit);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to read {} bytes from chained stream [{}]", buffer.GetCount(), SystemError(hr));
            return hr;
        }

        ULONGLONG ullRead = buffer.GetCount();

        size_t unitIndex = ((size_t)m_ullPosition / m_dwCompressionUnit) + i;

        if (!m_IsBlockCompressed.empty())
        {
            if (m_IsBlockCompressed[unitIndex])
            {
                // compression unit is compressed: proceed with decompression
                NTFS_COMP_INFO info;
                info.buf_size_b = m_dwCompressionUnit;
                info.comp_buf = (char*)buffer.GetData();
                info.comp_len = m_dwCompressionUnit;
                info.uncomp_buf = (char*)uncompressedData.GetData() + uncomp_processed;
                info.uncomp_idx = 0L;

                if (FAILED(hr = ntfs_uncompress_compunit(&info)))
                {
                    Log::Warn(
                        L"Failed to uncompress {} bytes from compressed unit, copying as raw data [{}]",
                        info.comp_len,
                        SystemError(hr));
                    CopyMemory(
                        (LPBYTE)uncompressedData.GetData() + uncomp_processed, buffer.GetData(), m_dwCompressionUnit);
                    uncomp_processed += m_dwCompressionUnit;
                }
                else
                {
                    uncomp_processed += info.comp_len;
                }
            }
            else
            {
                CopyMemory(
                    (LPBYTE)uncompressedData.GetData() + uncomp_processed, buffer.GetData(), m_dwCompressionUnit);
                uncomp_processed += m_dwCompressionUnit;
            }
        }
        else
        {
            // compression unit is compressed: proceed with decompression
            NTFS_COMP_INFO info;
            info.buf_size_b = m_dwCompressionUnit;
            info.comp_buf = (char*)buffer.GetData();
            info.comp_len = m_dwCompressionUnit;
            info.uncomp_buf = (char*)uncompressedData.GetCount() + uncomp_processed;
            info.uncomp_idx = 0L;

            if (FAILED(hr = ntfs_uncompress_compunit(&info)))
            {
                // If decompression failed and CUs not compressed information is not available, we assume the CU was
                // not compressed
                Log::Warn(
                    "Failed to uncompress {} bytes from compressed unit, copying as raw/uncompressed data [{}]",
                    info.comp_len,
                    SystemError(hr));
                CopyMemory(
                    (LPBYTE)uncompressedData.GetData() + uncomp_processed, buffer.GetData(), m_dwCompressionUnit);
                uncomp_processed += m_dwCompressionUnit;
            }
            else
            {
                uncomp_processed += info.comp_len;
            }
        }
    }

    if (pcbBytesRead)
        *pcbBytesRead = uncomp_processed;
    return S_OK;
}

HRESULT UncompressNTFSStream::ReadRaw(CBinaryBuffer& buffer, size_t length)
{
    if (length == 0LL)
    {
        return S_OK;
    }

    buffer.SetCount(RoundUp(length, 65536));

    size_t totalRead = 0;
    while (totalRead < length)
    {
        if (m_CurrentSegmentIndex >= m_DataSegments.size())
        {
            break;
        }

        if (m_CurrentSegmentOffset
            >= (m_bAllocatedData ? m_DataSegments[m_CurrentSegmentIndex].ullAllocatedSize
                                 : m_DataSegments[m_CurrentSegmentIndex].ullSize))
        {
            m_CurrentSegmentIndex++;  // We have reached the end of the segment, moving to the next
            m_CurrentSegmentOffset = 0LL;
        }

        if (m_CurrentSegmentIndex >= m_DataSegments.size())
        {
            break;
        }

        auto dataSize = (m_bAllocatedData ? m_DataSegments[m_CurrentSegmentIndex].ullAllocatedSize
                                          : m_DataSegments[m_CurrentSegmentIndex].ullSize)
            - m_CurrentSegmentOffset;

        dataSize = RoundUpPow2(dataSize, 4096);

        size_t remaining = length - totalRead;
        size_t processed;
        if (dataSize < remaining)
        {
            remaining = dataSize;
        }

        if (m_DataSegments[m_CurrentSegmentIndex].bUnallocated || !m_DataSegments[m_CurrentSegmentIndex].bValidData)
        {
            auto output = BufferSpan(buffer.GetData() + totalRead, remaining);
            std::fill(std::begin(output), std::end(output), 0);
            processed = remaining;
        }
        else if (totalRead == 0)
        {
            std::error_code ec;
            BufferSpan output(buffer.GetData(), remaining);
            processed = Stream::ReadAt(
                *m_volume,
                m_DataSegments[m_CurrentSegmentIndex].ullDiskBasedOffset + m_CurrentSegmentOffset,
                output,
                ec);
            if (ec)
            {
                return ToHRESULT(ec);
            }
        }
        else
        {
            std::error_code ec;

            auto output = BufferSpan(buffer.GetData() + totalRead, buffer.GetCount() - totalRead);
            processed = Stream::ReadAt(
                *m_volume,
                m_DataSegments[m_CurrentSegmentIndex].ullDiskBasedOffset + m_CurrentSegmentOffset,
                output,
                ec);
            if (ec)
            {
                return ToHRESULT(ec);
            }
        }

        m_CurrentSegmentOffset += processed;
        m_CurrentPosition += processed;
        totalRead += std::min(remaining, processed);
    }

    if (totalRead > length)
    {
        Log::Warn(L"Unexpected NTFSStream::Read return value (expected: {}, current: {})", length, totalRead);
    }

    buffer.SetCount(totalRead);
    return S_OK;
}

HRESULT UncompressNTFSStream::Read_(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;
    if (cbBytesToRead > MAXDWORD)
    {
        return E_INVALIDARG;
    }

    if (pcbBytesRead != nullptr)
    {
        *pcbBytesRead = 0;
    }

    const uint64_t allocatedSize = GetSize();
    if ((cbBytesToRead + m_ullPosition) > allocatedSize)
    {
        cbBytesToRead = allocatedSize - m_ullPosition;
    }

    if (cbBytesToRead == 0)
    {
        return S_OK;
    }

    ULONGLONG ullBytesToRead =
        RoundUpPow2((m_ullPosition % m_dwCompressionUnit) + cbBytesToRead, static_cast<ULONGLONG>(m_dwCompressionUnit));
    DWORD dwCUsToRead =
        static_cast<DWORD>(ullBytesToRead / m_dwCompressionUnit + (ullBytesToRead % m_dwCompressionUnit > 0 ? 1 : 0));

    ULONGLONG ullToRead = 0LLU;
    if (!msl::utilities::SafeMultiply((ULONGLONG)dwCUsToRead, m_dwCompressionUnit, ullToRead))
    {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }

    ULONGLONG ullRead = 0LL;
    if (m_ullPosition % m_dwCompressionUnit == 0)
    {
        if (cbBytesToRead >= ullToRead)
        {
            CBinaryBuffer buffer((LPBYTE)pBuffer, (size_t)cbBytesToRead);
            hr = ReadCompressionUnit(dwCUsToRead, buffer, &ullRead);
            if (FAILED(hr))
            {
                return hr;
            }
        }
        else
        {
            CBinaryBuffer buffer;
            buffer.SetCount((size_t)ullToRead);
            hr = ReadCompressionUnit(dwCUsToRead, buffer, &ullRead);
            if (FAILED(hr))
            {
                return hr;
            }

            CopyMemory(pBuffer, buffer.GetData(), static_cast<size_t>(std::min(cbBytesToRead, ullRead)));
            ullRead = std::min<ULONGLONG>(cbBytesToRead, buffer.GetCount());
        }
    }
    else
    {
        // the read is _NOT_ aligned on a CU.
        size_t offset = m_ullPosition % m_dwCompressionUnit;

        CBinaryBuffer buffer;
        buffer.SetCount((size_t)ullToRead);
        hr = ReadCompressionUnit(dwCUsToRead, buffer, &ullRead);
        if (FAILED(hr))
        {
            return hr;
        }

        CopyMemory(
            pBuffer,
            buffer.GetData() + offset,
            static_cast<size_t>(std::min<ULONGLONG>(cbBytesToRead, ullRead - offset)));
        ullRead = std::min<ULONGLONG>(cbBytesToRead, ullRead - offset);
    }

    if (pcbBytesRead != nullptr)
    {
        *pcbBytesRead = ullRead;
    }

    m_ullPosition += ullRead;

    if (m_ullPosition % m_dwCompressionUnit && m_ullPosition < GetSize())
    {
        // We end up in the middle of a CU, we need to rewind the underlying (compressed) stream by 1 CU...
        SetFilePointer(-static_cast<LONGLONG>(m_dwCompressionUnit), FILE_CURRENT, NULL);
    }

    return S_OK;
}

HRESULT UncompressNTFSStream::SetFilePointer(
    __in LONGLONG lDistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pqwCurrPointer)
{
    LONGLONG llCUAlignedPosition = 0L;

    switch (dwMoveMethod)
    {
        case FILE_BEGIN:
            m_ullPosition = lDistanceToMove;
            break;
        case FILE_CURRENT:
            m_ullPosition += lDistanceToMove;
            break;
        case FILE_END:
            m_ullPosition = GetSize() + lDistanceToMove;
            break;
    }

    if (m_ullPosition > GetSize())
    {
        m_ullPosition = GetSize();
    }

    if (pqwCurrPointer != nullptr)
    {
        *pqwCurrPointer = m_ullPosition;
    }

    ULONG64 ullChainedPosition = 0LL;
    llCUAlignedPosition = (m_ullPosition / m_dwCompressionUnit) * m_dwCompressionUnit;
    return NTFSStream::SetFilePointer(llCUAlignedPosition, FILE_BEGIN, &ullChainedPosition);
}
