//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "UncompressNTFSStream.h"

#include "NTFSCompression.h"

#include "NTFSStream.h"
#include "VolumeReader.h"

using namespace Orc;

UncompressNTFSStream::UncompressNTFSStream()
    : ChainingStream()
    , m_ullPosition(0L)
{
    m_dwCompressionUnit = 0L;
    m_dwMaxCompressionUnit = 0L;
}

UncompressNTFSStream::~UncompressNTFSStream(void) {}

HRESULT UncompressNTFSStream::Close()
{
    if (m_pChainedStream == nullptr)
        return S_OK;
    return m_pChainedStream->Close();
}

HRESULT UncompressNTFSStream::Open(const std::shared_ptr<ByteStream>& pChained, DWORD dwCompressionUnit)
{
    if (pChained == NULL)
        return E_POINTER;

    if (pChained->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream must be opened");
        return E_FAIL;
    }

    m_pChainedStream = pChained;

    switch (dwCompressionUnit)
    {
            // Valid CU values are:
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

    m_dwMaxCompressionUnit =
        (DWORD)(pChained->GetSize() / m_dwCompressionUnit) + ((pChained->GetSize() % m_dwCompressionUnit) ? 1 : 0);

    m_ullPosition = 0LL;
    return S_OK;
}

HRESULT UncompressNTFSStream::Open(const std::shared_ptr<NTFSStream>& pChained, DWORD dwCompressionUnit)
{
    HRESULT hr = E_FAIL;

    const std::shared_ptr<ByteStream>& pByteStream = pChained;

    if (FAILED(hr = Open(pByteStream, dwCompressionUnit)))
        return hr;

    const auto segments = pChained->DataSegments();

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
    else
    {
        return S_OK;
    }
}

HRESULT UncompressNTFSStream::ReadCompressionUnit(
    DWORD dwNbCU,
    CBinaryBuffer& uncompressedData,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (pcbBytesRead)
        *pcbBytesRead = 0LL;

    if (dwNbCU > m_dwMaxCompressionUnit)
    {
        Log::Error(
            "Attempted to read past the last compression unit (tried {}, max is {})", dwNbCU, m_dwMaxCompressionUnit);
        return hr;
    }

    ULONGLONG ullRead = 0LL;
    ULONGLONG ullToRead = static_cast<ULONGLONG>(dwNbCU) * m_dwCompressionUnit;

    CBinaryBuffer buffer(true);
    if (!buffer.SetCount(static_cast<size_t>(ullToRead)))
        return E_OUTOFMEMORY;

    while (ullRead < (m_dwCompressionUnit * static_cast<ULONGLONG>(dwNbCU)) && ullRead < ullToRead)
    {
        ULONGLONG ullThisRead = 0LL;
        if (FAILED(hr = m_pChainedStream->Read(buffer.GetData() + ullRead, buffer.GetCount() - ullRead, &ullThisRead)))
        {
            Log::Error(L"Failed to read {} bytes from chained stream (code: {:#x})", buffer.GetCount() - ullRead, hr);
            return hr;
        }
        if (ullThisRead == 0LL)
            break;
        ullRead += ullThisRead;
    }

    if (ullRead == 0LL)
    {
        buffer.RemoveAll();
        return S_OK;
    }

    size_t uncomp_processed = 0;

    for (unsigned int i = 0; i < dwNbCU; i++)
    {
        size_t unitIndex = ((size_t)m_ullPosition / m_dwCompressionUnit) + i;

        if (ullRead - uncomp_processed >= m_dwCompressionUnit)
        {
            if (!m_IsBlockCompressed.empty())
            {
                if (m_IsBlockCompressed[unitIndex])
                {
                    // compression unit is compressed: proceed with decompression
                    NTFS_COMP_INFO info;
                    info.buf_size_b = m_dwCompressionUnit;
                    info.comp_buf = (char*)buffer.GetData() + uncomp_processed;
                    info.comp_len = m_dwCompressionUnit;
                    info.uncomp_buf = (char*)uncompressedData.GetData() + uncomp_processed;
                    info.uncomp_idx = 0L;

                    if (FAILED(hr = ntfs_uncompress_compunit(&info)))
                    {
                        Log::Warn(
                            L"Failed to uncompress {} bytes from compressed unit, copying as raw/uncompressed data "
                            L"(code: {:#x})",
                            info.comp_len,
                            hr);
                        CopyMemory(
                            (LPBYTE)uncompressedData.GetData() + uncomp_processed,
                            (char*)buffer.GetData() + uncomp_processed,
                            m_dwCompressionUnit);
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
                        (LPBYTE)uncompressedData.GetData() + uncomp_processed,
                        (char*)buffer.GetData() + uncomp_processed,
                        m_dwCompressionUnit);
                    uncomp_processed += m_dwCompressionUnit;
                }
            }
            else
            {
                // compression unit is compressed: proceed with decompression
                NTFS_COMP_INFO info;
                info.buf_size_b = m_dwCompressionUnit;
                info.comp_buf = (char*)buffer.GetData() + uncomp_processed;
                info.comp_len = m_dwCompressionUnit;
                info.uncomp_buf = (char*)uncompressedData.GetCount() + uncomp_processed;
                info.uncomp_idx = 0L;

                if (FAILED(hr = ntfs_uncompress_compunit(&info)))
                {
                    // If decompression failed and CUs not compressed information is not available, we assume the CU was
                    // not compressed
                    Log::Warn(
                        "Failed to uncompress {} bytes from compressed unit, copying as raw/uncompressed data (code: "
                        "{:#x})",
                        info.comp_len,
                        hr);
                    CopyMemory(
                        (LPBYTE)uncompressedData.GetData() + uncomp_processed,
                        (char*)buffer.GetData() + uncomp_processed,
                        m_dwCompressionUnit);
                    uncomp_processed += m_dwCompressionUnit;
                }
                else
                {
                    uncomp_processed += info.comp_len;
                }
            }
        }
        else
        {
            if (!m_IsBlockCompressed.empty())
            {
                // the last uncompression has to take place in a buffer of at least CU size
                if (m_IsBlockCompressed[unitIndex])
                {
                    CBinaryBuffer uncomp(true);
                    uncomp.SetCount(m_dwCompressionUnit);

                    NTFS_COMP_INFO info;
                    info.buf_size_b = m_dwCompressionUnit;
                    info.comp_buf = (char*)buffer.GetData() + uncomp_processed;
                    info.comp_len = m_dwCompressionUnit;
                    info.uncomp_buf = (char*)uncomp.GetData();
                    info.uncomp_idx = 0L;

                    if (FAILED(hr = ntfs_uncompress_compunit(&info)))
                    {
                        Log::Warn(
                            L"Failed to uncompress {} bytes from compressed unit, copying as raw/uncompressed data "
                            L"(code: {:#x})",
                            info.comp_len,
                            hr);
                        CopyMemory(
                            (LPBYTE)uncompressedData.GetData() + uncomp_processed,
                            (char*)buffer.GetData() + uncomp_processed,
                            m_dwCompressionUnit);
                        uncomp_processed += m_dwCompressionUnit;
                    }
                    else
                    {
                        CopyMemory(
                            uncompressedData.GetData() + uncomp_processed,
                            uncomp.GetData(),
                            static_cast<size_t>(ullRead - uncomp_processed));
                        uncomp_processed += static_cast<size_t>(ullRead - uncomp_processed);
                    }
                }
                else
                {
                    CopyMemory(
                        uncompressedData.GetData() + uncomp_processed,
                        (char*)buffer.GetData() + uncomp_processed,
                        static_cast<size_t>(ullRead - uncomp_processed));
                    uncomp_processed += static_cast<size_t>(ullRead - uncomp_processed);
                }
            }
            else
            {
                CBinaryBuffer uncomp(true);
                uncomp.SetCount(m_dwCompressionUnit);

                NTFS_COMP_INFO info;
                info.buf_size_b = m_dwCompressionUnit;
                info.comp_buf = (char*)buffer.GetData() + uncomp_processed;
                info.comp_len = m_dwCompressionUnit;
                info.uncomp_buf = (char*)uncomp.GetData();
                info.uncomp_idx = 0L;

                if (FAILED(hr = ntfs_uncompress_compunit(&info)))
                {
                    // If decompression failed and CUs not compressed information is not available, we assume the CU was
                    // not compressed
                    CopyMemory(
                        (LPBYTE)uncompressedData.GetData() + uncomp_processed,
                        (char*)buffer.GetData() + uncomp_processed,
                        static_cast<size_t>(ullRead - uncomp_processed));
                    uncomp_processed += m_dwCompressionUnit;
                }
                else
                {
                    CopyMemory(
                        uncompressedData.GetData() + uncomp_processed,
                        uncomp.GetData(),
                        static_cast<size_t>(ullRead - uncomp_processed));
                    uncomp_processed += static_cast<size_t>(ullRead - uncomp_processed);
                }
            }
        }
    }
    if (pcbBytesRead)
        *pcbBytesRead = uncomp_processed;
    return S_OK;
}

HRESULT UncompressNTFSStream::Read(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;
    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead != nullptr)
        *pcbBytesRead = 0;

    ULONGLONG ullDataSize = m_pChainedStream->GetSize();

    if ((cbBytesToRead + m_ullPosition) > ullDataSize)
        cbBytesToRead = ullDataSize - m_ullPosition;

    if (cbBytesToRead == 0)
    {
        if (pcbBytesRead != NULL)
            *pcbBytesRead = 0LL;
        return S_OK;
    }

    ULONGLONG ullBytesToRead = (m_ullPosition % m_dwCompressionUnit) + cbBytesToRead;
    DWORD dwCUsToRead =
        static_cast<DWORD>(ullBytesToRead / m_dwCompressionUnit + (ullBytesToRead % m_dwCompressionUnit > 0 ? 1 : 0));
    ULONGLONG ullToRead = 0LLU;

    if (!msl::utilities::SafeMultiply((ULONGLONG)dwCUsToRead, m_dwCompressionUnit, ullToRead))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    ULONGLONG ullRead = 0LL;

    if (m_ullPosition % m_dwCompressionUnit == 0)
    {
        if (cbBytesToRead >= ullToRead)
        {
            CBinaryBuffer buffer((LPBYTE)pBuffer, (size_t)cbBytesToRead);
            if (FAILED(hr = ReadCompressionUnit(dwCUsToRead, buffer, &ullRead)))
                return hr;
        }
        else
        {
            CBinaryBuffer buffer;
            buffer.SetCount((size_t)ullToRead);
            if (FAILED(hr = ReadCompressionUnit(dwCUsToRead, buffer, &ullRead)))
                return hr;
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
        if (FAILED(hr = ReadCompressionUnit(dwCUsToRead, buffer, &ullRead)))
            return hr;
        CopyMemory(
            pBuffer,
            buffer.GetData() + offset,
            static_cast<size_t>(std::min<ULONGLONG>(cbBytesToRead, ullRead - offset)));
        ullRead = std::min<ULONGLONG>(cbBytesToRead, ullRead - offset);
    }

    if (pcbBytesRead != nullptr)
        *pcbBytesRead = ullRead;
    m_ullPosition += ullRead;

    if (m_ullPosition % m_dwCompressionUnit && m_ullPosition < m_pChainedStream->GetSize())
    {
        // We end up in the middle of a CU, we need to rewind the underlying (compressed) stream by 1 CU...
        m_pChainedStream->SetFilePointer(-static_cast<LONGLONG>(m_dwCompressionUnit), FILE_CURRENT, NULL);
    }
    return S_OK;
}

HRESULT UncompressNTFSStream::Write(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesWritten)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBytes);
    DBG_UNREFERENCED_PARAMETER(pcbBytesWritten);

    return E_NOTIMPL;
}

HRESULT UncompressNTFSStream::SetFilePointer(
    __in LONGLONG lDistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pqwCurrPointer)
{
    if (!m_pChainedStream)
        return E_FAIL;

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
            m_ullPosition = m_pChainedStream->GetSize() + lDistanceToMove;
            break;
    }

    if (m_ullPosition > m_pChainedStream->GetSize())
        m_ullPosition = m_pChainedStream->GetSize();

    if (pqwCurrPointer != nullptr)
        *pqwCurrPointer = m_ullPosition;

    ULONG64 ullChainedPosition = 0LL;
    llCUAlignedPosition = (m_ullPosition / m_dwCompressionUnit) * m_dwCompressionUnit;
    return m_pChainedStream->SetFilePointer(llCUAlignedPosition, FILE_BEGIN, &ullChainedPosition);
}

ULONG64 UncompressNTFSStream::GetSize()
{
    if (m_pChainedStream != nullptr)
        return m_pChainedStream->GetSize();
    LARGE_INTEGER Size = {0};
    return Size.QuadPart;
}

HRESULT UncompressNTFSStream::SetSize(ULONG64 ullNewSize)
{
    DBG_UNREFERENCED_PARAMETER(ullNewSize);

    return S_OK;
}
