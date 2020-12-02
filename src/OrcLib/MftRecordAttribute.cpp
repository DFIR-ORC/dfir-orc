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

#include "SystemDetails.h"

#include "Log/Log.h"

using namespace std;

using namespace Orc;

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

HRESULT MftRecordAttribute::GetStreams(const std::shared_ptr<VolumeReader>& pVolReader)
{
    std::shared_ptr<ByteStream> rawStream, dataStream;
    return GetStreams(pVolReader, rawStream, dataStream);
}

HRESULT MftRecordAttribute::GetStreams(
    const std::shared_ptr<VolumeReader>& pVolReader,
    std::shared_ptr<ByteStream>& rawStream,
    std::shared_ptr<ByteStream>& dataStream)
{
    HRESULT hr = E_FAIL;

    if (m_Details != nullptr && m_Details->GetDataStream() != nullptr && m_Details->GetRawStream() != nullptr)
    {
        rawStream = m_Details->GetRawStream();
        dataStream = m_Details->GetRawStream();
    }

    _ASSERT(pVolReader);

    _ASSERT(m_pHeader != nullptr);

    if (m_pHeader->FormCode == NONRESIDENT_FORM)
    {
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
                    L"INFO: Collect record with unsupported compression unit value: {}",
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
                    Log::Error(L"Failed to open NTFSStream [{}]", SystemError(hr));
                    return hr;
                }

                auto datastream = make_shared<UncompressNTFSStream>();
                if (FAILED(hr = datastream->Open(rawdata, 16 * pVolReader->GetBytesPerCluster())))
                {
                    Log::Error(L"Failed to open UncompressNTFSStream [{}]", SystemError(hr));
                    return hr;
                }
                else if (hr == S_FALSE)
                {
                    auto pFN = m_pHostRecord->GetDefaultFileName();
                    if (pFN != nullptr && pFN->FileNameLength <= pVolReader->MaxComponentLength())
                    {
                        std::wstring_view fileName(pFN->FileName, pFN->FileNameLength);
                        Log::Debug(
                            L"Issues when opening compressed file: '{}' with record {:#x})",
                            fileName,
                            m_pHostRecord->GetSafeMFTSegmentNumber());
                    }
                    else
                    {
                        Log::Debug(
                            L"Issues when opening compressed file with record {:#x})",
                            m_pHostRecord->GetSafeMFTSegmentNumber());
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
        auto stream = make_shared<BufferStream<MAX_PATH>>();

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

    if (const auto stream = m_Details->GetDataStream())
        return stream;

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

    if (const auto stream = m_Details->GetRawStream())
        return stream;

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
        Log::Debug(L"Failed to seek pointer to 0 for data attribute [{}]", SystemError(hr));
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
        Log::Debug(L"Failed to seek pointer to 0 for data attribute [{}]", SystemError(hr));
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
        WCHAR szName[MAX_PATH];

        if (FAILED(hr = AnsiToWide(pEAInfo->EaName, pEAInfo->EaNameLength, szName, MAX_PATH)))
            return hr;

        m_Items.push_back(pair<wstring, CBinaryBuffer>(
            wstring(szName),
            CBinaryBuffer(((BYTE*)pEAInfo->EaName) + pEAInfo->EaNameLength + 1, pEAInfo->EaValueLength)));

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
