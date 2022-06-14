//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "MFTOnline.h"

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

            // Get all the extents (runs) associated with the MFT itself.
            if (FAILED(hr = GetMFTExtents(m_pVolReader->GetBootSector())))
                break;

            m_pFetchReader = m_pVolReader->ReOpen(
                FILE_READ_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS);

        } while (false);
    }
    catch (...)
    {
        Log::Info(L"Exception thrown when processing MFT");
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
        Log::Error(L"Failed to read MFT record!");
        return hr;
    }

    // Fixup the record buffer
    MFTRecord mftRecord;
    mftRecord.ParseRecord(
        m_pVolReader,
        reinterpret_cast<FILE_RECORD_SEGMENT_HEADER*>(record.GetData()), record.GetCount(),
        NULL);

    PFILE_RECORD_SEGMENT_HEADER pData = (PFILE_RECORD_SEGMENT_HEADER)record.GetData();

    if (!(pData->Flags & FILE_RECORD_SEGMENT_IN_USE))
    {
        Log::Error("MFT record marked as 'not in use'");
        return E_UNEXPECTED;
    }

    realLength = (ULONG)record.GetCount();

    if (pData->FirstAttributeOffset > realLength)
    {
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
        Log::Error(
            L"MFT Record length is smaller than First attribute Offset ({}/{})",
            realLength,
            pData->FirstAttributeOffset);
        return hr;
    }

    PATTRIBUTE_RECORD_HEADER pAttrData = (PATTRIBUTE_RECORD_HEADER)((LPBYTE)pData + pData->FirstAttributeOffset);
    ULONG nAttrLength = realLength - pData->FirstAttributeOffset;

    //
    // Go through each attribute and locate the one we are interested in.
    //
    hr = E_FAIL;
    while (nAttrLength >= sizeof(ATTRIBUTE_RECORD_HEADER))
    {
        if (pAttrData->TypeCode == $END)
        {
            break;
        }
        if (pAttrData->RecordLength == 0)
        {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            Log::Error(L"MFT - an attribute's (Attribute TypeCode: {:#x}) Record length is zero", pAttrData->TypeCode);
        }

        if ($DATA == pAttrData->TypeCode)
        {
            if (pAttrData->FormCode == RESIDENT_FORM)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                Log::Error(L"MFT - not expecting data attribute to be in resident form");
                break;
            }
            else  // non-resident form
            {
                m_MFT0Info.DataSize = pAttrData->Form.Nonresident.ValidDataLength;
                if (FAILED(hr = MFTUtils::GetAttributeNRExtents(pAttrData, m_MFT0Info, m_pVolReader)))
                {
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    Log::Error("MFT - failed to get non resident extents");
                    break;
                }
                hr = S_OK;
                break;
            }
        }

        if (pAttrData->RecordLength > nAttrLength)
        {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            Log::Error(
                L"MFT - an attribute's (Attribute TypeCode = {:#x}) Record length ({}) is greater than the attribute "
                L"length ({}).",
                pAttrData->TypeCode,
                pAttrData->RecordLength,
                nAttrLength);
            break;
        }
        else
        {
            PATTRIBUTE_RECORD_HEADER pNextAttrData =
                (PATTRIBUTE_RECORD_HEADER)((LPBYTE)pAttrData + pAttrData->RecordLength);  // ready for next record
            nAttrLength -= pAttrData->RecordLength;
            pAttrData = pNextAttrData;
        }
    }

    if (FAILED(hr))
        return hr;

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

    for (auto iter = m_MFT0Info.ExtentsVector.begin(); iter != m_MFT0Info.ExtentsVector.end(); ++iter)
    {
        MFTUtils::NonResidentAttributeExtent& NRAE = *iter;

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
                    extent_position,
                    ulBytesPerFRS * ullFRSToRead,
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

                if (ullCurrentIndex != ullCurrentFRNIndex)
                {
                    Log::Debug("Current index does not match current FRN index");
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

                Log::Warn("Add Record Callback failed [{}]", SystemError(hr));
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
