//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "MFTOnline.h"
#include "LogFileWriter.h"
#include "VolumeReader.h"

using namespace Orc;

static const auto DEFAULT_FRS_PER_READ = 64;

MFTOnline::MFTOnline(logger pLog, std::shared_ptr<VolumeReader>& volReader)
    : m_pVolReader(volReader)
    , _L_(std::move(pLog))
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
        log::Info(_L_, L"Exception thrown when processing MFT!!!\r\n");
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
        log::Error(_L_, hr, L"Invalid NTFS volume\r\n");
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
            log::Error(_L_, hr, L"Failed to read the %u MFT record!\r\n", i);
            break;
        }
        if (FAILED(hr = m_pVolReader->Read(Offset2, mftMirrBuf, ulBytesPerFRS, ullBytesRead)))
        {
            log::Error(_L_, hr, L"Failed to read the %u Mirror MFT record!\r\n", i);
            break;
        }

        if (0 != memcmp(mftBuf.GetData(), mftMirrBuf.GetData(), ulBytesPerFRS))
        {
            log::Warning(
                _L_, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), L"Records #%u from $MFT and $MFTMirr do not match\r\n", i);
        }

        PFILE_RECORD_SEGMENT_HEADER pData = (PFILE_RECORD_SEGMENT_HEADER)mftBuf.GetData();

        if ((pData->MultiSectorHeader.Signature[0] != 'F') || (pData->MultiSectorHeader.Signature[1] != 'I')
            || (pData->MultiSectorHeader.Signature[2] != 'L') || (pData->MultiSectorHeader.Signature[3] != 'E'))
        {
            log::Error(
                _L_, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), L"MFT rec (%u) doesn't have a file signature!\r\n", i);
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
        log::Error(_L_, hr, L"Failed to read MFT record!\r\n");
        return hr;
    }

    PFILE_RECORD_SEGMENT_HEADER pData = (PFILE_RECORD_SEGMENT_HEADER)record.GetData();

    if (!(pData->Flags & FILE_RECORD_SEGMENT_IN_USE))
    {
        log::Error(_L_, E_UNEXPECTED, L"MFT record marked as not in use!\r\n");
        return E_UNEXPECTED;
    }

    realLength = (ULONG)record.GetCount();

    if (pData->FirstAttributeOffset > realLength)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            L"MFT Record length (%u) is smaller than First attribute Offset (%u)!\r\n",
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
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                L"MFT - an attribute's (Attribute TypeCode = %x) Record length is zero.\r\n",
                pAttrData->TypeCode);
        }

        if ($DATA == pAttrData->TypeCode)
        {
            if (pAttrData->FormCode == RESIDENT_FORM)
            {
                log::Error(
                    _L_,
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                    L"MFT - not expecting data attribute to be in resident form!\r\n");
                break;
            }
            else  // non-resident form
            {
                m_MFT0Info.DataSize = pAttrData->Form.Nonresident.ValidDataLength;
                if (FAILED(hr = MFTUtils::GetAttributeNRExtents(pAttrData, m_MFT0Info, m_pVolReader)))
                {
                    log::Error(
                        _L_,
                        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                        L"MFT - failed to get non resident extents!\r\n");
                    break;
                }
                hr = S_OK;
                break;
            }
        }

        if (pAttrData->RecordLength > nAttrLength)
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                L"MFT - an attribute's (Attribute TypeCode = 0x%lx) Record length (%u) is greater than the attribute "
                L"length (%u).\r\n",
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
            log::Error(_L_, hr, L"Failed to read the %u MFT record!\n", i);
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
        log::Error(_L_, E_FAIL, L"Invalid NTFS volume\r\n");
        return 0;
    }

    if (ullMFTSize % ulBytesPerFRS)
    {
        log::Verbose(_L_, L"Weird, MFT size is not a multiple of records...\r\n");
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
                log::Error(
                    _L_,
                    hr,
                    L"Failed to read %I64d bytes from at position %I64d\r\n",
                    extent_position,
                    ulBytesPerFRS * ullFRSToRead);
                return hr;
            }

            if (ullBytesRead % ulBytesPerFRS > 0)
            {
                log::Warning(
                    _L_,
                    hr,
                    L"Failed to read only complete records\r\n",
                    extent_position,
                    ulBytesPerFRS * ullFRSToRead);
            }

            for (unsigned int i = 0; i < (ullBytesRead / ulBytesPerFRS); i++)
            {
                if (ullCurrentIndex * ulBytesPerFRS != position + (i * ulBytesPerFRS))
                {
                    log::Warning(_L_, E_FAIL, L"Index is out of sequence\r\n");
                }

                CBinaryBuffer tempFRS(localReadBuffer.GetData() + i * ulBytesPerFRS, ulBytesPerFRS);

                if (FAILED(hr = pCallBack(ullCurrentFRNIndex, tempFRS)))
                {
                    if (hr == E_OUTOFMEMORY)
                    {
                        log::Error(_L_, hr, L"Add Record Callback failed, not enough memory to continue\r\n");
                        return hr;
                    }
                    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
                    {
                        log::Verbose(_L_, L"Add Record Callback asks for enumeration to stop...\r\n");
                        return hr;
                    }
                    log::Verbose(_L_, L"WARNING: Add Record Callback failed\r\n");
                }

                if (ullCurrentIndex != ullCurrentFRNIndex)
                {
                    log::Verbose(_L_, L"Current index does not match current FRN index\r\n");
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
                log::Error(
                    _L_, hr, L"Failed to read %I64d bytes from at position %I64d\r\n", ullVolumeOffset, ulBytesPerFRS);
                break;
            }

            PFILE_RECORD_SEGMENT_HEADER pHeader = (PFILE_RECORD_SEGMENT_HEADER)localReadBuffer.GetData();

            if ((pHeader->MultiSectorHeader.Signature[0] != 'F') || (pHeader->MultiSectorHeader.Signature[1] != 'I')
                || (pHeader->MultiSectorHeader.Signature[2] != 'L') || (pHeader->MultiSectorHeader.Signature[3] != 'E'))
            {
                log::Verbose(
                    _L_,
                    L"Skipping... MultiSectorHeader.Signature is not FILE - \"%c%c%c%c\".\r\n",
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
                log::Verbose(
                    _L_,
                    L"Skipping... %I64X does not match the expected %I64X\r\n",
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
                log::Verbose(
                    _L_,
                    L"Skipping... Sequence numbed %d does not match the expected %d\r\n",
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
                    log::Error(_L_, hr, L"Add Record Callback failed, not enough memory to continue\r\n");
                    break;
                }
                else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
                {
                    log::Verbose(_L_, L"Add Record Callback asks for enumeration to stop...\r\n");
                    return hr;
                }
                log::Verbose(_L_, L"WARNING: Add Record Callback failed\r\n");
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
