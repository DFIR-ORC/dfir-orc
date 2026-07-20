//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2023 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "UncompressNTFSStream.h"

#include "MftRecordAttribute.h"
#include "NTFSCompression.h"
#include "VolumeReader.h"
#include "Utils/Round.h"
#include "Utils/BufferSpan.h"
#include "Stream/StreamUtils.h"
#include "Stream/VolumeStreamReader.h"

using namespace Orc;

namespace {

// ReadRaw shrinks the buffer to the bytes actually read, which falls short of a full unit once the runlist is
// exhausted. Consumers read a whole unit, so pad the missing tail with zeroes (as ReadRaw itself does for
// unallocated segments) to keep the whole unit initialized and in bounds.
bool PadCompressionUnit(CBinaryBuffer& buffer, size_t unitSize)
{
    const size_t read = buffer.GetCount();
    if (read >= unitSize)
    {
        return true;
    }

    if (!buffer.SetCount(unitSize))
    {
        return false;
    }

    ZeroMemory(buffer.GetData() + read, unitSize - read);
    return true;
}

}  // namespace

UncompressNTFSStream::UncompressNTFSStream()
    : NTFSStream()
    , m_volume(nullptr)
    , m_ullPosition(0L)
    , m_uncompressedSize(0)
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

    DWORDLONG uncompressedSize = 0;
    hr = pDataAttr->DataSize(m_pVolReader, uncompressedSize);
    if (FAILED(hr))
    {
        return hr;
    }
    m_uncompressedSize = uncompressedSize;

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
        size_t curSegment = 0;
        ULONGLONG ullInSegmentOffset = 0;

        for (unsigned int curCU = 0; curCU < m_dwMaxCompressionUnit; curCU++)
        {
            DWORD dwInCUOffset = 0;
            ULONGLONG ullSegmentSize = 0LL;

            // the unit count derives from the declared FileSize, the segments from the runlist: they may disagree
            if (curSegment >= segments.size())
            {
                break;
            }

            const auto& currentSegment = segments[curSegment];

            ullSegmentSize = currentSegment.ullAllocatedSize;

            if ((ullSegmentSize - ullInSegmentOffset) >= m_dwCompressionUnit)
            {
                CUMethod[curCU] = false;
                ullInSegmentOffset += m_dwCompressionUnit;
                if (ullInSegmentOffset >= ullSegmentSize)
                {
                    curSegment++;
                    ullInSegmentOffset = 0LL;
                }
            }
            else
            {
                bool bCUHasData = false;
                bool bCUHasSparse = false;

                DWORD dwThisCUInSegmentOffset = 0L;

                while (dwInCUOffset < m_dwCompressionUnit && curSegment < segments.size())
                {
                    if (segments[curSegment].bUnallocated)
                        bCUHasSparse = true;
                    else
                        bCUHasData = true;

                    const ULONGLONG ullAvailableChunk = ullSegmentSize - ullInSegmentOffset;
                    DWORD dwChunk = 0L;

                    if (ullAvailableChunk > (m_dwCompressionUnit - dwInCUOffset))
                    {
                        dwChunk = m_dwCompressionUnit - dwInCUOffset;
                    }
                    else
                    {
                        // bounded by the branch above: fits a DWORD
                        dwChunk = static_cast<DWORD>(ullAvailableChunk);
                    }

                    dwInCUOffset += dwChunk % m_dwCompressionUnit;
                    dwThisCUInSegmentOffset += dwChunk % m_dwCompressionUnit;

                    ullInSegmentOffset += dwThisCUInSegmentOffset;

                    if (ullInSegmentOffset >= ullSegmentSize)
                    {
                        curSegment++;
                        ullInSegmentOffset = 0LL;
                        dwThisCUInSegmentOffset = 0L;

                        if (curSegment < segments.size())
                            ullSegmentSize = segments[curSegment].ullAllocatedSize;
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
    uncompressedData.ZeroMe();

    CBinaryBuffer buffer(true);
    for (size_t i = 0; i < dwNbCU; ++i)
    {
        hr = ReadRaw(buffer, m_dwCompressionUnit);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to read {} bytes from chained stream [{}]", buffer.GetCount(), SystemError(hr));
            return hr;
        }

        if (buffer.GetCount() == 0)
        {
            // the runlist is exhausted: this unit holds no data at all, do not fabricate one
            break;
        }

        // establishes buffer.GetCount() >= m_dwCompressionUnit for every read of 'buffer' below
        if (!PadCompressionUnit(buffer, m_dwCompressionUnit))
        {
            return E_OUTOFMEMORY;
        }

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

                    uint64_t copyLength = std::min(
                        static_cast<uint64_t>(m_dwCompressionUnit),
                        static_cast<uint64_t>(uncompressedData.GetCount() - uncomp_processed));

                    CopyMemory((LPBYTE)uncompressedData.GetData() + uncomp_processed, buffer.GetData(), copyLength);
                    uncomp_processed += copyLength;
                }
                else
                {
                    uncomp_processed += info.comp_len;
                }
            }
            else
            {
                uint64_t copyLength = std::min(
                    static_cast<uint64_t>(m_dwCompressionUnit),
                    static_cast<uint64_t>(uncompressedData.GetCount() - uncomp_processed));

                CopyMemory((LPBYTE)uncompressedData.GetData() + uncomp_processed, buffer.GetData(), copyLength);
                uncomp_processed += copyLength;
            }
        }
        else
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
                // If decompression failed and CUs not compressed information is not available, we assume the CU was
                // not compressed
                Log::Warn(
                    "Failed to uncompress {} bytes from compressed unit, copying as raw/uncompressed data [{}]",
                    info.comp_len,
                    SystemError(hr));

                uint64_t copyLength = std::min(
                    static_cast<uint64_t>(m_dwCompressionUnit),
                    static_cast<uint64_t>(uncompressedData.GetCount() - uncomp_processed));

                CopyMemory((LPBYTE)uncompressedData.GetData() + uncomp_processed, buffer.GetData(), copyLength);
                uncomp_processed += copyLength;
            }
            else
            {
                uncomp_processed += info.comp_len;
            }
        }

        if (uncomp_processed == uncompressedData.GetCount())
        {
            break;
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

            auto output = BufferSpan(buffer.GetData() + totalRead, remaining);
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

    // As it could be required to read more bytes than caller specified because of block compression the file offset
    // must be updated for any following read.
    hr = SetFilePointer(m_ullPosition, FILE_BEGIN, NULL);
    if (FAILED(hr))
    {
        Log::Error("Failed SetFilePointer [{}]", SystemError(hr));
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

Orc::UncompressNTFSStream::~UncompressNTFSStream()
{}
