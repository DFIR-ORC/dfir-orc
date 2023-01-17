//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "MFTOnline.h"

#include <boost/algorithm/string/join.hpp>

#include "VolumeReader.h"

#include "Log/Log.h"
#include "MFTRecord.h"

using namespace Orc;

static const auto DEFAULT_FRS_PER_READ = 64;

MFTOnline::MFTOnline(std::shared_ptr<VolumeReader> volReader)
    : m_pVolReader(std::move(volReader))
{
    m_MftOffset = 0LL;
    m_RootUSN = 0LL;
}

MFTOnline::~MFTOnline() {}

HRESULT MFTOnline::Initialize()
{
    HRESULT hr = E_FAIL;

    try
    {
        do
        {
            // Discover where the mft is on the drive and validate it
            if (FAILED(hr = m_pVolReader->LoadDiskProperties()))
                return hr;

            m_pFetchReader = m_pVolReader->ReOpen(
                FILE_READ_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS);

            // Get all the extents (runs) associated with the MFT itself.
            if (FAILED(hr = GetMFTExtents(m_pVolReader->GetBootSector())))
                break;
        } while (false);
    }
    catch (...)
    {
        Log::Info(L"Exception thrown when processing MFT");
    }

    return S_OK;
}

HRESULT MFTOnline::GetMFTChildsRecordExtents(
    const std::shared_ptr<VolumeReader>& volume,
    MFTRecord& mftRecord,
    MFTUtils::NonResidentDataAttrInfo& extentsInfo)
{
    struct Child
    {
        MFTUtils::SafeMFTSegmentNumber segment;
        VCN lowerVCN;
    };

    std::vector<Child> childs;
    for (const auto& attributeListEntry : mftRecord.GetAttributeList())
    {
        if (attributeListEntry.TypeCode() != $DATA)
        {
            continue;
        }

        if (attributeListEntry.LowestVCN() == 0)
        {
            continue;
        }

        childs.emplace_back(Child {attributeListEntry.HostRecordSegmentNumber(), attributeListEntry.LowestVCN()});
    }

    std::sort(std::begin(childs), std::end(childs), [](const auto& lhs, const auto& rhs) {
        return lhs.lowerVCN < rhs.lowerVCN;
    });

    for (auto it = std::cbegin(childs); it != std::cend(childs); ++it)
    {
        bool hasFetchedRecord = false;

        MFT_SEGMENT_REFERENCE frn;
        frn.SegmentNumberLowPart = it->segment & 0xFFFFFFFF;
        frn.SegmentNumberHighPart = (it->segment >> 32) & 0x0000FFFF;
        frn.SequenceNumber = (it->segment >> 48);

        HRESULT hr = FetchMFTRecord(
            frn,
            [&](MFTUtils::SafeMFTSegmentNumber& ulRecordIndex, CBinaryBuffer& childRecordBuffer) -> HRESULT {
                MFTRecord childRecord;
                HRESULT hr = childRecord.ParseRecord(
                    volume,
                    reinterpret_cast<FILE_RECORD_SEGMENT_HEADER*>(childRecordBuffer.GetData()),
                    childRecordBuffer.GetCount(),
                    &mftRecord);

                bool hasFoundDataAttribute = false;
                for (auto& attribute : childRecord.GetAttributeList())
                {
                    if (attribute.TypeCode() != $DATA)
                    {
                        continue;
                    }

                    if (hasFoundDataAttribute)
                    {
                        Log::Error("Found $MFT child record with multiple attributes $DATA");
                    }

                    hasFoundDataAttribute = true;

                    hr = MFTUtils::GetAttributeNRExtents(attribute.Attribute()->Header(), extentsInfo, volume);
                    if (FAILED(hr))
                    {
                        Log::Error(L"Failed to parse extent from $MFT child record [{}]", SystemError(hr));
                        return hr;
                    }
                }

                if (!hasFoundDataAttribute)
                {
                    Log::Error("$MFT child record has no $DATA attribute ({:#x})", it->segment);
                }

                return S_OK;
            },
            hasFetchedRecord);

        if (FAILED(hr))
        {
            Log::Error(L"Failed to parse $MFT child record (frn: {:#x}) [{}]", it->segment, SystemError(hr));
            continue;  // Get as many extent as possible
        }

        if (!hasFetchedRecord)
        {
            Log::Error(L"Failed to fetch $MFT child record (frn: {:#x}) [{}]", it->segment, SystemError(hr));
            continue;
        }
    }

    return S_OK;
}

// Retrieves the extents of MFT itself.
HRESULT MFTOnline::GetMFTExtents(const CBinaryBuffer& buffer)
{
    ULONG realLength = 0;
    HRESULT hr = E_FAIL;

    ULONG ulBytesPerFRS = m_pVolReader->GetBytesPerFRS();
    ULONG ulBytesPerCluster = m_pVolReader->GetBytesPerCluster();

    if (ulBytesPerFRS == 0 || ulBytesPerCluster == 0)
    {
        Log::Error("Invalid NTFS volume");
        return hr;
    }

    PackedBootSector pbs;
    ZeroMemory(&pbs, sizeof(PackedBootSector));
    CopyMemory(&pbs, buffer.GetData(), sizeof(PackedBootSector));

    m_MftOffset = ulBytesPerCluster * (ULONGLONG)(pbs.MftStartLcn);

    // validate mft by checking if mft rec matches the mftmirror
    ULONGLONG MftMirrorOffset = ulBytesPerCluster * (ULONGLONG)(pbs.Mft2StartLcn);

    UINT i = 0;
    for (; i < 4; i++)
    {
        ULONGLONG Offset1 = m_MftOffset + (i * ulBytesPerFRS);
        ULONGLONG Offset2 = MftMirrorOffset + (i * ulBytesPerFRS);

        CBinaryBuffer mftBuf, mftMirrBuf;
        ULONGLONG ullBytesRead = 0LL;
        if (FAILED(hr = m_pVolReader->Read(Offset1, mftBuf, ulBytesPerFRS, ullBytesRead)))
        {
            Log::Error("Failed to read the {} MFT record [{}]", i, SystemError(hr));
            break;
        }
        if (FAILED(hr = m_pVolReader->Read(Offset2, mftMirrBuf, ulBytesPerFRS, ullBytesRead)))
        {
            Log::Error(L"Failed to read the {} Mirror MFT record [{}]", i, SystemError(hr));
            break;
        }

        if (0 != memcmp(mftBuf.GetData(), mftMirrBuf.GetData(), ulBytesPerFRS))
        {
            Log::Warn(L"Records #{} from $MFT and $MFTMirr do not match", i);
        }

        PFILE_RECORD_SEGMENT_HEADER pData = (PFILE_RECORD_SEGMENT_HEADER)mftBuf.GetData();

        if ((pData->MultiSectorHeader.Signature[0] != 'F') || (pData->MultiSectorHeader.Signature[1] != 'I')
            || (pData->MultiSectorHeader.Signature[2] != 'L') || (pData->MultiSectorHeader.Signature[3] != 'E'))
        {
            Log::Error(L"MFT record #{} doesn't have a file signature", i);
            break;
        }
    }

    if (i < 4)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    // Now read the MFT record itself (to determine its extents)
    ULONGLONG ullBytesRead = 0LL;
    CBinaryBuffer record;

    if (FAILED(hr = m_pVolReader->Read(m_MftOffset, record, ulBytesPerFRS, ullBytesRead)))
    {
        Log::Error(L"Failed to read $MFT record!");
        return hr;
    }

    // Fixup the record buffer
    MFTRecord mftRecord;
    mftRecord.ParseRecord(
        m_pVolReader, reinterpret_cast<FILE_RECORD_SEGMENT_HEADER*>(record.GetData()), record.GetCount(), NULL);

    auto dataAttribute = mftRecord.GetDataAttribute(L"");
    if (!dataAttribute)
    {
        Log::Error(L"Failed to read $MFT:$DATA");
        return E_FAIL;
    }

    hr = MFTUtils::GetAttributeNRExtents(dataAttribute->Header(), m_MFT0Info, m_pVolReader);
    if (FAILED(hr))
    {
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        Log::Error("Failed to read extents from $MFT base record");
        return hr;
    }

    m_MFT0Info.DataSize = dataAttribute->GetNonResidentInformation(m_pVolReader)->ValidDataSize;

    if (!mftRecord.GetChildRecords().empty())
    {
        hr = GetMFTChildsRecordExtents(m_pVolReader, mftRecord, m_MFT0Info);
        if (FAILED(hr))
        {
            Log::Error("Failed to read some $MFT child records [{}]", SystemError(hr));
            hr = S_OK;  // continue: get as many data as possible
        }
    }

    {
        ULONGLONG Offset = m_MftOffset + (5 * ulBytesPerFRS);
        CBinaryBuffer RootRecordBuffer;
        ULONGLONG ullBytesRead = 0LL;

        if (FAILED(hr = m_pVolReader->Read(Offset, RootRecordBuffer, ulBytesPerFRS, ullBytesRead)))
        {
            Log::Error(L"Failed to read the {} MFT record [{}]", i, SystemError(hr));
            return hr;
        }

        PFILE_RECORD_SEGMENT_HEADER pHeader = (PFILE_RECORD_SEGMENT_HEADER)RootRecordBuffer.GetData();

        MFT_SEGMENT_REFERENCE RootReference;

        RootReference.SegmentNumberHighPart = pHeader->SegmentNumberHighPart;
        RootReference.SegmentNumberLowPart = pHeader->SegmentNumberLowPart;
        RootReference.SequenceNumber = pHeader->SequenceNumber;

        m_RootUSN = NtfsFullSegmentNumber(&RootReference);
    }

    return hr;
}

ULONG MFTOnline::GetMFTRecordCount() const
{
    ULONGLONG ullMFTSize = 0L;

    // go through each extent of mft and search for the files
    for (auto iter = m_MFT0Info.ExtentsVector.begin(); iter != m_MFT0Info.ExtentsVector.end(); ++iter)
    {
        const MFTUtils::NonResidentAttributeExtent& NRAE = *iter;

        ullMFTSize += NRAE.DataSize;
    }

    ULONG ulBytesPerFRS = m_pVolReader->GetBytesPerFRS();

    if (ulBytesPerFRS == 0)
    {
        Log::Error(L"Invalid NTFS volume");
        return 0;
    }

    if (ullMFTSize % ulBytesPerFRS)
    {
        Log::Debug("Weird, MFT size is not a multiple of records...");
    }

    LARGE_INTEGER li;

    li.QuadPart = ullMFTSize / ulBytesPerFRS;
    if (li.QuadPart > MAXDWORD)
        return 0;
    return li.LowPart;
}

HRESULT MFTOnline::EnumMFTRecord(MFTUtils::EnumMFTRecordCall pCallBack)
{
    HRESULT hr = E_FAIL;

    if (pCallBack == nullptr)
        return E_POINTER;

    ULONGLONG ullCurrentIndex = 0LL;
    // go through each extent of mft and search for the files
    ULONGLONG ullCurrentFRNIndex = 0LL;

    ULONG ulBytesPerFRS = m_pVolReader->GetBytesPerFRS();

    CBinaryBuffer localReadBuffer(true);

    if (!localReadBuffer.CheckCount(ulBytesPerFRS * DEFAULT_FRS_PER_READ))
        return E_OUTOFMEMORY;

    ULONGLONG position = 0LL;

    for (auto& NRAE : m_MFT0Info.ExtentsVector)
    {

        if (NRAE.bZero)
            continue;

        ULONGLONG end = NRAE.DiskOffset + NRAE.DataSize;
        ULONGLONG extent_position = NRAE.DiskOffset;

        ULONGLONG ullFRSCountInExtent = NRAE.DataSize / ulBytesPerFRS;
        ULONGLONG ullFRSLeftToRead = ullFRSCountInExtent;

        while (extent_position < end)
        {
            ULONGLONG ullFRSToRead = std::min<ULONGLONG>(ullFRSLeftToRead, DEFAULT_FRS_PER_READ);

            LARGE_INTEGER liBytesToRead;
            liBytesToRead.QuadPart = ulBytesPerFRS * ullFRSToRead;

            if (liBytesToRead.HighPart > 0)
                return TYPE_E_SIZETOOBIG;

            if (!localReadBuffer.CheckCount(static_cast<size_t>(ulBytesPerFRS * ullFRSToRead)))
                return E_OUTOFMEMORY;

            ULONGLONG ullBytesRead = 0LL;
            if (FAILED(
                    hr = m_pVolReader->Read(
                        extent_position, localReadBuffer, ulBytesPerFRS * ullFRSToRead, ullBytesRead)))
            {
                Log::Error(
                    L"Failed to read {} bytes from at position {} [{}]",
                    ulBytesPerFRS * ullFRSToRead,
                    extent_position,
                    SystemError(hr));
                return hr;
            }

            if (ullBytesRead % ulBytesPerFRS > 0)
            {
                Log::Warn(
                    L"Failed to read only complete records {} at position {}",
                    extent_position,
                    ulBytesPerFRS * ullFRSToRead);
            }

            for (unsigned int i = 0; i < (ullBytesRead / ulBytesPerFRS); i++)
            {
                if (ullCurrentIndex * ulBytesPerFRS != position + (i * ulBytesPerFRS))
                {
                    Log::Warn("Index is out of sequence");
                }

                CBinaryBuffer tempFRS(localReadBuffer.GetData() + i * ulBytesPerFRS, ulBytesPerFRS);

                if (FAILED(hr = pCallBack(ullCurrentFRNIndex, tempFRS)))
                {
                    if (hr == E_OUTOFMEMORY)
                    {
                        Log::Error("Add Record Callback failed, not enough memory to continue [{}]", SystemError(hr));
                        return hr;
                    }
                    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
                    {
                        Log::Debug("Add Record Callback asks for enumeration to stop [{}]", SystemError(hr));
                        return hr;
                    }
                    Log::Warn("Add Record Callback failed [{}]", SystemError(hr));
                }

                ullCurrentFRNIndex++;
                ullCurrentIndex++;
            }
            extent_position += ullBytesRead;
            position += ullBytesRead;
        }
    }

    return S_OK;
}

HRESULT
MFTOnline::FetchMFTRecord(MFT_SEGMENT_REFERENCE& frn, MFTUtils::EnumMFTRecordCall pCallBack, bool& hasFoundRecord)
{
    hasFoundRecord = false;

    auto callback = [&](MFTUtils::SafeMFTSegmentNumber& ulRecordIndex, CBinaryBuffer& data) -> HRESULT {
        hasFoundRecord = true;
        return pCallBack(ulRecordIndex, data);
    };

    return FetchMFTRecord(std::vector<MFT_SEGMENT_REFERENCE> {frn}, callback);
}

HRESULT MFTOnline::FetchMFTRecord(std::vector<MFT_SEGMENT_REFERENCE>& frn, MFTUtils::EnumMFTRecordCall pCallBack)
{
    HRESULT hr = E_FAIL;

    if (pCallBack == nullptr)
        return E_POINTER;
    if (frn.empty())
        return S_OK;

    std::sort(begin(frn), end(frn), [](const MFT_SEGMENT_REFERENCE& left, const MFT_SEGMENT_REFERENCE& rigth) -> bool {
        if (left.SegmentNumberHighPart != rigth.SegmentNumberHighPart)
            return left.SegmentNumberHighPart < rigth.SegmentNumberHighPart;
        return left.SegmentNumberLowPart < rigth.SegmentNumberLowPart;
    });

    // go through each extent of mft and search for the files

    ULONG ulBytesPerFRS = m_pVolReader->GetBytesPerFRS();

    CBinaryBuffer localReadBuffer(true);

    if (!localReadBuffer.CheckCount(ulBytesPerFRS))
        return E_OUTOFMEMORY;

    UINT frnIdx = 0;
    ULONGLONG ullCurrentIndex = 0;

    for (const auto& NRAE : m_MFT0Info.ExtentsVector)
    {
        ULONGLONG ullEnd = ullCurrentIndex + NRAE.DataSize;

        if (NRAE.bZero)
            continue;

        ULARGE_INTEGER current;
        current.HighPart = frn[frnIdx].SegmentNumberHighPart;
        current.LowPart = frn[frnIdx].SegmentNumberLowPart;

        while (ullCurrentIndex <= (current.QuadPart * ulBytesPerFRS) && (current.QuadPart * ulBytesPerFRS) < ullEnd)
        {
            // this segment contains the first FRN
            ULONGLONG ullVolumeOffset = NRAE.DiskOffset + ((current.QuadPart * ulBytesPerFRS) - ullCurrentIndex);

            ULONGLONG ullBytesRead = 0LL;
            if (FAILED(hr = m_pFetchReader->Read(ullVolumeOffset, localReadBuffer, ulBytesPerFRS, ullBytesRead)))
            {
                Log::Error(
                    L"Failed to read {} bytes from at position {} [{}]",
                    ullVolumeOffset,
                    ulBytesPerFRS,
                    SystemError(hr));
                break;
            }

            PFILE_RECORD_SEGMENT_HEADER pHeader = (PFILE_RECORD_SEGMENT_HEADER)localReadBuffer.GetData();

            if ((pHeader->MultiSectorHeader.Signature[0] != 'F') || (pHeader->MultiSectorHeader.Signature[1] != 'I')
                || (pHeader->MultiSectorHeader.Signature[2] != 'L') || (pHeader->MultiSectorHeader.Signature[3] != 'E'))
            {
                Log::Debug(
                    L"Skipping... MultiSectorHeader.Signature is not FILE - '{}{}{}{}'",
                    pHeader->MultiSectorHeader.Signature[0],
                    pHeader->MultiSectorHeader.Signature[1],
                    pHeader->MultiSectorHeader.Signature[2],
                    pHeader->MultiSectorHeader.Signature[3]);
                frnIdx++;
                if (frnIdx >= frn.size())
                    break;
                current.HighPart = frn[frnIdx].SegmentNumberHighPart;
                current.LowPart = frn[frnIdx].SegmentNumberLowPart;
            }

            MFT_SEGMENT_REFERENCE read_record_frn = {0};
            read_record_frn.SegmentNumberHighPart = pHeader->SegmentNumberHighPart;
            read_record_frn.SegmentNumberLowPart = pHeader->SegmentNumberLowPart;
            read_record_frn.SequenceNumber = pHeader->SequenceNumber;

            if (NtfsSegmentNumber(&read_record_frn) != NtfsSegmentNumber(&frn[frnIdx]))
            {
                Log::Debug(
                    L"Skipping... {} does not match the expected {}",
                    NtfsSegmentNumber(&read_record_frn),
                    NtfsSegmentNumber(&frn[frnIdx]));
                frnIdx++;
                if (frnIdx >= frn.size())
                    break;
                current.HighPart = frn[frnIdx].SegmentNumberHighPart;
                current.LowPart = frn[frnIdx].SegmentNumberLowPart;
                continue;
            }
            if (read_record_frn.SequenceNumber != frn[frnIdx].SequenceNumber)
            {
                Log::Debug(
                    L"Skipping... Sequence numbed {} does not match the expected {}",
                    read_record_frn.SequenceNumber,
                    frn[frnIdx].SequenceNumber);
                frnIdx++;
                if (frnIdx >= frn.size())
                    break;
                current.HighPart = frn[frnIdx].SegmentNumberHighPart;
                current.LowPart = frn[frnIdx].SegmentNumberLowPart;
                continue;
            }

            if (FAILED(hr = pCallBack(current.QuadPart, localReadBuffer)))
            {
                if (hr == E_OUTOFMEMORY)
                {
                    Log::Error("Add Record Callback failed, not enough memory to continue");
                    break;
                }
                else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
                {
                    Log::Debug("Add Record Callback asks for enumeration to stop...");
                    return hr;
                }

                Log::Error("Add Record Callback failed [{}]", SystemError(hr));
                return hr;
            }
            frnIdx++;
            if (frnIdx >= frn.size())
                break;
            current.HighPart = frn[frnIdx].SegmentNumberHighPart;
            current.LowPart = frn[frnIdx].SegmentNumberLowPart;
        }
        // this segment must be skipped
        ullCurrentIndex = ullEnd;
        if (frnIdx >= frn.size())
            break;
    }
    if (frnIdx >= frn.size())
    {
        frn.clear();
    }

    return S_OK;
}
