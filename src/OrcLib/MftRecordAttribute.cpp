//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include <sstream>

#include "MFTRecord.h"
#include "MftRecordAttribute.h"

#include "VolumeReader.h"
#include "WideAnsi.h"

#include "BufferStream.h"
#include "NTFSStream.h"
#include "UncompressNTFSStream.h"
#include "UncompressWofStream.h"
#include "Filesystem/Ntfs/Attribute/ReparsePoint/WofReparsePoint.h"
#include "SystemDetails.h"
#include "Log/Log.h"

using namespace std;

using namespace Orc;

namespace {

const std::shared_ptr<Orc::DataAttribute> GetWofDataAttribute(const Orc::MFTRecord& record)
{
    const auto si = record.GetStandardInformation();
    if (!si)
    {
        Log::Error(L"Invalid WOF record: expect $DATA with Standard Information");
        return nullptr;
    }

    if ((si->FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != FILE_ATTRIBUTE_SPARSE_FILE)
    {
        Log::Error(L"Invalid WOF record: expect FILE_ATTRIBUTE_SPARSE_FILE");
        return nullptr;
    }

    auto attribute = record.GetDataAttribute(L"");
    if (!attribute)
    {
        Log::Error("Invalid WOF record: missing '$DATA' attribute");
        return nullptr;
    }

    if (!attribute->IsNonResident())
    {
        Log::Error(L"Invalid WOF record: expect 'non resident' $DATA attribute");
        return nullptr;
    }

    if (attribute->Header()->Form.Nonresident.TotalAllocated != 0)
    {
        Log::Error(L"Invalid WOF record: expect $DATA's 'TotalAllocated = 0'");
        return nullptr;
    }

    if ((attribute->Header()->Flags & ATTRIBUTE_FLAG_SPARSE) != ATTRIBUTE_FLAG_SPARSE)
    {
        Log::Error(L"Invalid WOF record: expect $DATA with ATTRIBUTE_FLAG_SPARSE");
        return nullptr;
    }

    return attribute;
}

std::shared_ptr<WOFReparseAttribute> GetWofReparsePoint(const Orc::MFTRecord& record)
{
    assert(record.IsParsed());  // This is 'ParseRecord' which populate 'm_pAttributeList'

    for (const auto& attribute : record.GetAttributeList())
    {
        const auto wofReparsePoint =
            std::dynamic_pointer_cast<WOFReparseAttribute, MftRecordAttribute>(attribute.Attribute());
        if (wofReparsePoint)
        {
            return wofReparsePoint;
        }
    }

    return nullptr;
}

}  // namespace

HRESULT MftRecordAttribute::AddContinuationAttribute(const shared_ptr<MftRecordAttribute>& pMftRecordAttribute)
{
    if (m_pHeader == NULL || pMftRecordAttribute == NULL)
        return E_POINTER;

    _ASSERT(m_pHeader->TypeCode == pMftRecordAttribute->m_pHeader->TypeCode);
    _ASSERT(m_pHeader->NameLength == pMftRecordAttribute->m_pHeader->NameLength);
    _ASSERT(!wcsncmp(
        (LPCWSTR)(((BYTE*)m_pHeader) + m_pHeader->NameOffset),
        (LPCWSTR)(((BYTE*)pMftRecordAttribute->m_pHeader) + pMftRecordAttribute->m_pHeader->NameOffset),
        m_pHeader->NameLength));

    // moving pCur to the 'rigth' place
    shared_ptr<MftRecordAttribute> pCur = shared_from_this();

    // need to continue until next is 'bigger' than the one to insert
    auto pContAttr = pCur->m_pContinuationAttribute.lock();
    while (pContAttr != nullptr)
    {
        if (pContAttr->m_LowestVcn == pMftRecordAttribute->m_LowestVcn)
            return S_OK;  // for some reason, this continuation attribute is already added
        if (pContAttr->m_LowestVcn > pMftRecordAttribute->m_LowestVcn)
            break;

        pCur = pContAttr;
        pContAttr = pContAttr->m_pContinuationAttribute.lock();
    }

    // we 'insert' this attribute
    pMftRecordAttribute->m_pContinuationAttribute = pCur->m_pContinuationAttribute;
    pCur->m_pContinuationAttribute = pMftRecordAttribute;

    if (m_pNonResidentInfo != NULL)
    {
        m_pNonResidentInfo.reset();
        m_pNonResidentInfo = NULL;
        m_bNonResidentInfoPresent = false;
    }

    return S_OK;
}

HRESULT MftRecordAttribute::DataSize(const std::shared_ptr<VolumeReader>& VolReader, DWORDLONG& dwDataSize)
{
    dwDataSize = 0;

    if (m_pHeader->FormCode == RESIDENT_FORM)
    {
        dwDataSize = m_pHeader->Form.Resident.ValueLength;
    }
    else
    {
        if (m_pHeader->Form.Nonresident.LowestVcn == 0)
            dwDataSize = m_pHeader->Form.Nonresident.FileSize;
        else
        {
            if (!IsContinuation())
            {
                MFTUtils::NonResidentDataAttrInfo* pNRDAI = GetNonResidentInformation(VolReader);
                if (pNRDAI != nullptr)
                    dwDataSize = pNRDAI->DataSize;
                else
                    return E_FAIL;
            }
            else
            {
                dwDataSize = 0;
            }
        }
    }
    return S_OK;
}

HRESULT MftRecordAttribute::AllocatedSize(const std::shared_ptr<VolumeReader>& VolReader, DWORDLONG& dwAllocSize)
{
    dwAllocSize = 0;

    if (m_pHeader->FormCode == RESIDENT_FORM)
    {
        dwAllocSize = m_pHeader->Form.Resident.ValueLength;
    }
    else
    {
        if (m_pHeader->Form.Nonresident.LowestVcn == 0)
            dwAllocSize = m_pHeader->Form.Nonresident.FileSize;
        else
        {
            if (!IsContinuation())
            {
                MFTUtils::NonResidentDataAttrInfo* pNRDAI = GetNonResidentInformation(VolReader);
                if (pNRDAI != nullptr)
                    dwAllocSize = pNRDAI->DataSize;
                else
                    return E_FAIL;
            }
            else
            {
                dwAllocSize = 0;
            }
        }
    }
    return S_OK;
}

MFTUtils::NonResidentDataAttrInfo*
MftRecordAttribute::GetNonResidentInformation(const std::shared_ptr<VolumeReader>& pVolReader)
{
    HRESULT hr = E_FAIL;

    if (m_bNonResidentInfoPresent)
        return m_pNonResidentInfo.get();

    if (m_pHeader == NULL)
        return NULL;

    if (m_pHeader->FormCode == RESIDENT_FORM)
        return NULL;

    _ASSERT(m_pHeader->FormCode == NONRESIDENT_FORM);

    _ASSERT(m_pHeader->Form.Nonresident.LowestVcn == 0);

    m_pNonResidentInfo = std::make_unique<MFTUtils::NonResidentDataAttrInfo>();
    if (m_pNonResidentInfo == NULL)
        return NULL;

    shared_ptr<MftRecordAttribute> pCurAttr = shared_from_this();

    if (pCurAttr->m_pHeader->Form.Nonresident.LowestVcn == 0)
        m_pNonResidentInfo->DataSize = pCurAttr->m_pHeader->Form.Nonresident.FileSize;

    while (pCurAttr != NULL)
    {
        if (FAILED(hr = MFTUtils::GetAttributeNRExtents(pCurAttr->m_pHeader, *m_pNonResidentInfo, pVolReader)))
            return NULL;

        auto contAttr = pCurAttr->m_pContinuationAttribute.lock();
        std::swap(pCurAttr, contAttr);
    }

    if (m_pNonResidentInfo->DataSize > m_pNonResidentInfo->ExtentsSize)
    {
        // pad zeros at the end of all continuation attributes
        m_pNonResidentInfo->ExtentsVector.push_back(MFTUtils::NonResidentAttributeExtent(
            0,
            m_pNonResidentInfo->DataSize - m_pNonResidentInfo->ExtentsSize,
            m_pNonResidentInfo->DataSize - m_pNonResidentInfo->ExtentsSize,
            true));
        m_pNonResidentInfo->ExtentsSize += m_pNonResidentInfo->DataSize - m_pNonResidentInfo->ExtentsSize;
    }
    else if (m_pNonResidentInfo->DataSize < m_pNonResidentInfo->ExtentsSize)
    {
        // remove slack space...
        // From the end of the segments, we decrement DataSize to reflect the portion of the allocated segments actually
        // hold data
        ULONGLONG ullSlackSpace = m_pNonResidentInfo->ExtentsSize - m_pNonResidentInfo->DataSize;
        ULONGLONG ullSlacked = 0LL;
        std::for_each(
            m_pNonResidentInfo->ExtentsVector.rbegin(),
            m_pNonResidentInfo->ExtentsVector.rend(),
            [ullSlackSpace, &ullSlacked](MFTUtils::NonResidentAttributeExtent& extent) {
                if (extent.DiskAlloc > (ullSlackSpace - ullSlacked))
                {
                    extent.DataSize -= ullSlackSpace - ullSlacked;
                    ullSlacked += extent.DiskAlloc - extent.DataSize;
                }
                else
                {
                    extent.DataSize = 0LL;
                    extent.bZero = true;
                    ullSlacked += extent.DiskAlloc - extent.DataSize;
                }
            });
    }

    _ASSERT(m_pNonResidentInfo->DataSize <= m_pNonResidentInfo->ExtentsSize);

    m_bNonResidentInfoPresent = true;
    return m_pNonResidentInfo.get();
}

HRESULT MftRecordAttribute::GetNonResidentSegmentsToRead(
    const std::shared_ptr<VolumeReader>& VolReader,
    ULONGLONG ullStartOffset,
    ULONGLONG ullBytesToRead,
    ULONGLONG ullBlockSize,
    std::vector<MFTUtils::DataSegment>& ListOfSegments)
{
    HRESULT hr = E_FAIL;

    if (VolReader == NULL)
        return E_POINTER;

    _ASSERT(m_pHeader != nullptr);

    if (m_pHeader->FormCode == NONRESIDENT_FORM)
    {
        MFTUtils::NonResidentDataAttrInfo* DataAttrInfo = GetNonResidentInformation(VolReader);

        if (DataAttrInfo == NULL)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        hr = S_OK;

        return MFTUtils::GetDataSegmentsToRead(
            *DataAttrInfo, ullStartOffset, ullBytesToRead, ListOfSegments, ullBlockSize);
    }
    return S_OK;
}

const MFTRecord* MftRecordAttribute::GetBaseRecord() const
{
    if (m_pHostRecord == nullptr)
    {
        return nullptr;
    }

    return m_pHostRecord->IsBaseRecord() ? m_pHostRecord : m_pHostRecord->GetFileBaseRecord();
}

HRESULT MftRecordAttribute::GetStreams(const std::shared_ptr<VolumeReader>& pVolReader)
{
    std::shared_ptr<ByteStream> rawStream, dataStream;
    return GetStreams(pVolReader, rawStream, dataStream);
}

void MftRecordAttribute::LogStreamRequest(const MFTRecord* baseRecord) const
{
    if (!baseRecord)
    {
        return;
    }

    std::wstring_view filename;
    auto pFileName = baseRecord->GetDefaultFileName();
    if (pFileName)
    {
        filename = std::wstring_view(pFileName->FileName, pFileName->FileNameLength);
    }

    std::wstring_view attributeName(NamePtr(), NameLength());
    if (attributeName.empty())
    {
        Log::Debug(
            L"Getting new streams for '{}' (base frn: {:#x}, frn: {:#x})",
            filename,
            baseRecord ? NtfsFullSegmentNumber(&baseRecord->GetFileReferenceNumber()) : 0,
            m_pHostRecord ? NtfsFullSegmentNumber(&m_pHostRecord->GetFileReferenceNumber()) : 0);
    }
    else
    {
        Log::Debug(
            L"Getting new streams for '{}:{}' (base frn: {:#x}, frn: {:#x})",
            filename,
            attributeName,
            baseRecord ? NtfsFullSegmentNumber(&baseRecord->GetFileReferenceNumber()) : 0,
            m_pHostRecord ? NtfsFullSegmentNumber(&m_pHostRecord->GetFileReferenceNumber()) : 0);
    }
}

HRESULT MftRecordAttribute::GetStreams(
    const std::shared_ptr<VolumeReader>& pVolReader,
    std::shared_ptr<ByteStream>& rawStream,
    std::shared_ptr<ByteStream>& dataStream)
{
    HRESULT hr = E_FAIL;

    // BEWARE: I am not sure what is the purpose of this
    if (m_Details && m_Details->GetDataStream() && m_Details->GetRawStream()
        && m_Details->GetDataStream()->IsOpen() == S_OK && m_Details->GetRawStream()->IsOpen() == S_OK)
    {
        rawStream = m_Details->GetRawStream();
        dataStream = m_Details->GetDataStream();
        return S_OK;
    }

    _ASSERT(pVolReader);
    _ASSERT(m_pHeader != nullptr);

    std::wstring_view filename;

    const auto baseRecord = GetBaseRecord();
    LogStreamRequest(baseRecord);

    if (m_pHeader->FormCode == NONRESIDENT_FORM)
    {
        if (m_pHeader->NameLength == 0 && baseRecord && baseRecord->IsOverlayFile())
        {
            // Handle transparently $DATA for WofCompressedData files
            const auto wofReparsePoint = ::GetWofReparsePoint(*baseRecord);
            if (!wofReparsePoint)
            {
                Log::Error("Invalid WOF record: missing WOF reparse point");
                return E_FAIL;
            }

            hr = wofReparsePoint->GetStreams(pVolReader, rawStream, dataStream);
            if (FAILED(hr))
            {
                Log::Error("Failed to retrieve wof stream [{}]", SystemError(hr));
                return hr;
            }

            if (m_Details == nullptr)
            {
                m_Details = std::make_unique<DataDetails>();
            }

            m_Details->SetDataStream(dataStream);
            m_Details->SetRawStream(rawStream);
            return S_OK;
        }

        switch (m_pHeader->Form.Nonresident.CompressionUnit)
        {
            case 0: {
                auto stream = make_shared<NTFSStream>();
                if (FAILED(hr = stream->OpenStream(pVolReader, shared_from_this())))
                {
                    Log::Error("Failed to open NTFSStream [{}]", SystemError(hr));
                    return hr;
                }
                dataStream = rawStream = stream;
                if (m_Details == nullptr)
                    m_Details = std::make_unique<DataDetails>();
                if (m_Details != nullptr)
                {
                    m_Details->SetDataStream(dataStream);
                    m_Details->SetRawStream(rawStream);
                }
                return S_OK;
            }
            case 1:
            case 2:
            case 3: {
                Log::Info(
                    "Collect record with unsupported compression unit value: {}",
                    m_pHeader->Form.Nonresident.CompressionUnit);

                auto stream = make_shared<NTFSStream>();
                if (FAILED(hr = stream->OpenAllocatedDataStream(pVolReader, shared_from_this())))
                {
                    Log::Error(L"Failed to open NTFSStream [{}]", SystemError(hr));
                    return hr;
                }
                dataStream = rawStream = stream;
                if (m_Details == nullptr)
                    m_Details = std::make_unique<DataDetails>();
                if (m_Details != nullptr)
                {
                    m_Details->SetDataStream(dataStream);
                    m_Details->SetRawStream(rawStream);
                }
                return S_OK;
            }
            case 4: {
                auto rawdata = make_shared<NTFSStream>();
                if (FAILED(hr = rawdata->OpenAllocatedDataStream(pVolReader, shared_from_this())))
                {
                    Log::Error("Failed to open NTFSStream [{}]", SystemError(hr));
                    return hr;
                }

                auto datastream = make_shared<UncompressNTFSStream>();
                if (FAILED(hr = datastream->OpenAllocatedDataStream(pVolReader, shared_from_this())))
                {
                    Log::Error("Failed to open UncompressNTFSStream [{}]", SystemError(hr));
                    return hr;
                }
                else if (hr == S_FALSE)
                {
                    auto pFN = baseRecord->GetDefaultFileName();
                    if (pFN != nullptr && pFN->FileNameLength <= pVolReader->MaxComponentLength())
                    {
                        std::wstring_view fileName(pFN->FileName, pFN->FileNameLength);
                        Log::Debug(
                            L"Issues when opening compressed file: '{}' with record {:#x})",
                            fileName,
                            baseRecord->GetSafeMFTSegmentNumber());
                    }
                    else
                    {
                        Log::Debug(
                            L"Issues when opening compressed file with record {:#x})",
                            baseRecord->GetSafeMFTSegmentNumber());
                    }
                }
                dataStream = datastream;
                rawStream = rawdata;
                if (m_Details == nullptr)
                    m_Details = std::make_unique<DataDetails>();
                if (m_Details != nullptr)
                {
                    m_Details->SetDataStream(dataStream);
                    m_Details->SetRawStream(rawStream);
                }
                return S_OK;
            }
        }
    }
    else if (m_pHeader->FormCode == RESIDENT_FORM)
    {
        auto stream = make_shared<BufferStream<ORC_MAX_PATH>>();

        if (FAILED(hr = stream->Open()))
            return hr;

        ULONGLONG ullBytesWritten = 0LL;
        if (FAILED(
                hr = stream->Write(
                    ((PBYTE)m_pHeader) + m_pHeader->Form.Resident.ValueOffset,
                    m_pHeader->Form.Resident.ValueLength,
                    &ullBytesWritten)))
            return hr;

        if (ullBytesWritten != m_pHeader->Form.Resident.ValueLength)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        rawStream = dataStream = stream;
        if (m_Details == nullptr)
            m_Details = std::make_unique<DataDetails>();
        if (m_Details != nullptr)
        {
            m_Details->SetDataStream(dataStream);
            m_Details->SetRawStream(rawStream);
        }
        return S_OK;
    }
    return E_FAIL;
}

HRESULT MftRecordAttribute::CleanCachedData()
{
    if (m_pNonResidentInfo != NULL)
    {
        m_pNonResidentInfo.reset();
        m_pNonResidentInfo = NULL;
    }
    m_Details.reset();
    return S_OK;
}

std::shared_ptr<ByteStream> MftRecordAttribute::GetDataStream(const std::shared_ptr<VolumeReader>& pVolReader)
{
    HRESULT hr = E_FAIL;

    if (!m_Details)
        m_Details = std::make_unique<DataDetails>();

    const auto stream = m_Details->GetDataStream();
    if (stream)
    {
        if (stream->IsOpen() == S_OK)
        {
            return stream;
        }

        Log::Debug("A cached data stream was closed and is being reopened");
    }

    std::shared_ptr<ByteStream> rawStream, dataStream;

    if (SUCCEEDED(hr = GetStreams(pVolReader, rawStream, dataStream)))
        return dataStream;
    return nullptr;
}

std::shared_ptr<ByteStream> MftRecordAttribute::GetRawStream(const std::shared_ptr<VolumeReader>& pVolReader)
{
    HRESULT hr = E_FAIL;

    if (!m_Details)
        m_Details = std::make_unique<DataDetails>();

    const auto stream = m_Details->GetRawStream();
    if (stream)
    {
        if (stream->IsOpen() == S_OK)
        {
            return stream;
        }

        Log::Debug("A cached raw stream was closed and is being reopened");
    }

    std::shared_ptr<ByteStream> rawStream, dataStream;

    if (SUCCEEDED(hr = GetStreams(pVolReader, rawStream, dataStream)))
        return rawStream;
    return nullptr;
}

HRESULT MftRecordAttribute::GetHashInformation(
    const std::shared_ptr<VolumeReader>& pVolReader,
    CryptoHashStream::Algorithm required)
{
    HRESULT hr = E_FAIL;

    if (!m_Details)
        m_Details = std::make_unique<DataDetails>();

    CryptoHashStream::Algorithm needed = CryptoHashStream::Algorithm::Undefined;

    if (HasFlag(required, CryptoHashStream::Algorithm::MD5) && m_Details->MD5().empty())
        needed |= CryptoHashStream::Algorithm::MD5;
    if (HasFlag(required, CryptoHashStream::Algorithm::SHA1) && m_Details->SHA1().empty())
        needed |= CryptoHashStream::Algorithm::SHA1;
    if (HasFlag(required, CryptoHashStream::Algorithm::SHA256) && m_Details->SHA256().empty())
        needed |= CryptoHashStream::Algorithm::SHA256;

    if (needed == CryptoHashStream::Algorithm::Undefined)
        return S_OK;

    auto stream = GetDataStream(pVolReader);
    if (!stream)
        return E_FAIL;

    if (FAILED(hr = stream->SetFilePointer(0LL, SEEK_SET, nullptr)))
    {
        Log::Debug("Failed to seek pointer to 0 for data attribute [{}]", SystemError(hr));
        return hr;
    }

    shared_ptr<CryptoHashStream> pHashStream = make_shared<CryptoHashStream>();
    if (!pHashStream)
        return E_OUTOFMEMORY;

    if (FAILED(hr = pHashStream->OpenToWrite(needed, nullptr)))
        return hr;

    ULONGLONG ullWritten = 0LL;
    if (FAILED(hr = stream->CopyTo(pHashStream, &ullWritten)))
        return hr;

    if (FAILED(hr = stream->SetFilePointer(0LL, SEEK_SET, nullptr)))
    {
        Log::Debug("Failed to seek pointer to 0 for data attribute [{}]", SystemError(hr));
        return hr;
    }

    if (HasFlag(needed, CryptoHashStream::Algorithm::MD5))
    {
        CBinaryBuffer md5;
        if (FAILED(hr = pHashStream->GetMD5(md5)))
            return hr;
        m_Details->SetMD5(std::move(md5));
    }
    if (HasFlag(needed, CryptoHashStream::Algorithm::SHA1))
    {
        CBinaryBuffer sha1;
        if (FAILED(hr = pHashStream->GetSHA1(sha1)))
            return hr;
        m_Details->SetSHA1(std::move(sha1));
    }
    if (HasFlag(needed, CryptoHashStream::Algorithm::SHA256))
    {
        CBinaryBuffer sha256;
        if (FAILED(hr = pHashStream->GetSHA256(sha256)))
            return hr;
        m_Details->SetSHA256(std::move(sha256));
    }

    return S_OK;
}

HRESULT IndexRootAttribute::Open()
{
    if (m_pRoot != nullptr)
        return S_OK;
    if (m_pHeader->FormCode == NONRESIDENT_FORM)
    {
        switch (m_pHeader->Form.Nonresident.CompressionUnit)
        {
            case 0: {
                Log::Error("NON RESIDENT $INDEX_ROOT attribute are not supported");
                return E_NOTIMPL;
            }
            default: {
                Log::Error("Compressed $INDEX_ROOT attribute are not supported");
                return E_NOTIMPL;
            }
        }
    }
    else if (m_pHeader->FormCode == RESIDENT_FORM)
    {
        m_pRoot = (PINDEX_ROOT)((PBYTE)m_pHeader) + m_pHeader->Form.Resident.ValueOffset;
        return S_OK;
    }
    return S_OK;
}

HRESULT BitmapAttribute::LoadBitField(const std::shared_ptr<VolumeReader>& volreader)
{
    if (!m_bitset.empty())
        return S_OK;

    if (m_pHeader->FormCode == RESIDENT_FORM)
    {
        ULONG* pBlock = (PULONG)(((PBYTE)m_pHeader) + m_pHeader->Form.Resident.ValueOffset);

        size_t currentIndex = 0L;

        while (currentIndex < m_pHeader->Form.Resident.ValueLength)
        {
            m_bitset.append(*pBlock);
            pBlock++;
            currentIndex += sizeof(ULONG);
        }
        return S_OK;
    }
    else
    {
        HRESULT hr = S_OK;
        ULONGLONG ullBytesToRead = 0;
        ULONGLONG ullBytesRead = 0;

        if (FAILED(hr = DataSize(volreader, ullBytesToRead)))
            return hr;

        std::shared_ptr<MftRecordAttribute> attr = shared_from_this();

        CBinaryBuffer pData;
        pData.SetCount(static_cast<size_t>(ullBytesToRead));
        if (FAILED(hr = m_pHostRecord->ReadData(volreader, attr, 0, ullBytesToRead, pData, &ullBytesRead)))
            return hr;

        if (ullBytesToRead != ullBytesRead)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        for (size_t currentIndex = 0L; currentIndex < ullBytesRead / sizeof(ULONG); currentIndex++)
        {
            m_bitset.append(pData.Get<ULONG>(currentIndex));
        }
        return S_OK;
    }
}

HRESULT ExtendedAttribute::ReadData(const std::shared_ptr<VolumeReader>& VolReader)
{
    HRESULT hr = E_FAIL;
    ULONGLONG ullBytesToRead = 0;
    ULONGLONG ullBytesRead = 0;

    if (FAILED(hr = DataSize(VolReader, ullBytesToRead)))
        return hr;

    std::shared_ptr<MftRecordAttribute> attr = shared_from_this();

    if (FAILED(hr = m_pHostRecord->ReadData(VolReader, attr, 0, ullBytesToRead, m_RawData, &ullBytesRead)))
        return hr;

    if (ullBytesToRead == ullBytesRead)
        return S_OK;
    return E_FAIL;
}

HRESULT ExtendedAttribute::Parse(const std::shared_ptr<VolumeReader>& VolReader)
{
    if (m_bParsed)
        return S_OK;
    HRESULT hr = E_FAIL;
    if (FAILED(hr = ReadData(VolReader)))
        return hr;

    PFILE_FULL_EA_INFORMATION pEAInfo = EAInformation();
    PFILE_FULL_EA_INFORMATION pBase = pEAInfo;

    wstringstream EANameStream;

    while (pEAInfo < (PFILE_FULL_EA_INFORMATION)((BYTE*)pBase + EASize()))
    {
        WCHAR szName[ORC_MAX_PATH];

        if (FAILED(hr = AnsiToWide(pEAInfo->EaName, pEAInfo->EaNameLength, szName, ORC_MAX_PATH)))
            return hr;

        m_Items.push_back(pair<wstring, CBinaryBuffer>(
            wstring(szName),
            CBinaryBuffer(((BYTE*)pEAInfo->EaName) + pEAInfo->EaNameLength + 1, pEAInfo->EaValueLength)));

        if (pEAInfo->NextEntryOffset == 0)
        {
            break;
        }

        pEAInfo = (PFILE_FULL_EA_INFORMATION)((BYTE*)pEAInfo + pEAInfo->NextEntryOffset);
    }

    m_bParsed = true;
    return S_OK;
}

HRESULT ExtendedAttribute::CleanCachedData()
{
    MftRecordAttribute::CleanCachedData();
    m_Items.clear();
    m_RawData.RemoveAll();
    m_bParsed = false;
    return S_OK;
}

REPARSE_POINT_TYPE_AND_FLAGS Orc::ReparsePointAttribute::GetReparsePointType(PATTRIBUTE_RECORD_HEADER pHeader)
{
    if (pHeader->FormCode != RESIDENT_FORM)
        return (REPARSE_POINT_TYPE_AND_FLAGS)0L;

    PREPARSE_POINT_ATTRIBUTE pReparse =
        (PREPARSE_POINT_ATTRIBUTE)(((BYTE*)pHeader) + pHeader->Form.Resident.ValueOffset);

    return (REPARSE_POINT_TYPE_AND_FLAGS)pReparse->TypeAndFlags;
}

HRESULT ReparsePointAttribute::Parse()
{
    if (m_pHeader->FormCode != RESIDENT_FORM)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    m_Flags = GetReparsePointType(m_pHeader);

    PREPARSE_POINT_ATTRIBUTE pReparse =
        (PREPARSE_POINT_ATTRIBUTE)(((BYTE*)m_pHeader) + m_pHeader->Form.Resident.ValueOffset);

    PREPARSE_POINT_DATA pData = (PREPARSE_POINT_DATA)pReparse->Data;

    strSubstituteName.assign(
        (WCHAR*)((BYTE*)pData->Data + pData->SubstituteNameOffset), pData->SubstituteNameLength / sizeof(WCHAR));
    strPrintName.assign((WCHAR*)((BYTE*)pData->Data + pData->PrintNameOffset), pData->PrintNameLength / sizeof(WCHAR));

    return S_OK;
}

HRESULT ReparsePointAttribute::CleanCachedData()
{
    strSubstituteName.clear();
    strPrintName.clear();
    return S_OK;
}

WOFReparseAttribute::WOFReparseAttribute(PATTRIBUTE_RECORD_HEADER pHeader, MFTRecord* pRecord, std::error_code& ec)
    : ReparsePointAttribute(pHeader, pRecord)
    , m_algorithm(Ntfs::WofAlgorithm::kUnknown)
{
    PREPARSE_POINT_ATTRIBUTE pReparse =
        (PREPARSE_POINT_ATTRIBUTE)(((BYTE*)Header()) + Header()->Form.Resident.ValueOffset);

    BufferView repasePointView(reinterpret_cast<uint8_t*>(pReparse->Data), pReparse->DataLength);

    const auto wofHeader = Ntfs::WofReparsePoint::Parse(repasePointView);
    if (!wofHeader)
    {
        Log::Debug("Invalid wof header [{}]", wofHeader.error());
        ec = wofHeader.error();
        return;
    }

    m_algorithm = wofHeader.value().CompressionFormat();

    if (m_algorithm == Ntfs::WofAlgorithm::kLzx)
    {
        Log::Debug("Unsupported wof algorithm");
        ec = std::make_error_code(std::errc::function_not_supported);
        return;
    }
};

HRESULT WOFReparseAttribute::GetStreams(
    const std::shared_ptr<VolumeReader>& pVolReader,
    std::shared_ptr<ByteStream>& rawStream,
    std::shared_ptr<ByteStream>& dataStream)
{
    auto baseRecord = GetBaseRecord();
    if (!baseRecord)
    {
        Log::Error("Invalid WOF base record: nullptr");
        return E_FAIL;
    }

    auto dataAttribute = ::GetWofDataAttribute(*baseRecord);
    if (!dataAttribute)
    {
        Log::Error("Failed to retrieve wof $DATA attribute");
        return E_FAIL;
    }

    auto wofAttribute = baseRecord->GetDataAttribute(L"WofCompressedData");
    if (!wofAttribute)
    {
        Log::Error("Invalid WOF record: missing 'WofCompressedData' attribute");
        return E_FAIL;
    }

    auto ntfsStream = make_shared<NTFSStream>();
    HRESULT hr = ntfsStream->OpenAllocatedDataStream(pVolReader, wofAttribute);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to open NTFSStream [{}]", SystemError(hr));
        return hr;
    }

    // Nonresident: checked in '::GetWofDataAttribute()'
    Log::Debug(
        "Open WOF stream (algorithm: {}, compressed size: {} uncompressed size: {})",
        ToString(m_algorithm),
        ntfsStream->GetSize(),
        dataAttribute->Header()->Form.Nonresident.FileSize);

    auto wofStream = make_shared<UncompressWofStream>();
    hr = wofStream->Open(ntfsStream, m_algorithm, dataAttribute->Header()->Form.Nonresident.FileSize);
    if (FAILED(hr))
    {
        Log::Debug("Failed to open wof stream [{}]", SystemError(hr));
        return hr;
    }

    rawStream = ntfsStream;
    dataStream = wofStream;
    return S_OK;
}
