//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "MFTUtils.h"

#include "VolumeReader.h"

using namespace Orc;

inline auto IsCharLtrZero(DWORD C)
{
    return (((C)&0x00000080) == 0x00000080);
}

// Get the Non-resident extents of a given attribute.
// This is the routine used to get the details of MFT and data portion of other files.
// This algorithm is documented in details into NtfsDataStructures.h
HRESULT MFTUtils::GetAttributeNRExtents(
    PATTRIBUTE_RECORD_HEADER pAttrRecordHeader,
    NonResidentDataAttrInfo& FSRAttribInfo,
    const std::shared_ptr<VolumeReader>& pVolReader)
{
    //
    // Get the extents from the non-resident info
    //
    _ASSERT(pAttrRecordHeader->FormCode == NONRESIDENT_FORM);

    //  1.  Initialize:
    LONGLONG CurrentVcn = pAttrRecordHeader->Form.Nonresident.LowestVcn;
    LONGLONG CurrentLcn = 0;

    //  2.  Initialize byte stream pointer to: (PCHAR)Attribute +
    //  Attribute->AttributeForm->Nonresident->MappingPairsOffset
    LPBYTE pPairs = ((LPBYTE)pAttrRecordHeader + pAttrRecordHeader->Form.Nonresident.MappingPairsOffset);
    ULONG PairDataLen = pAttrRecordHeader->RecordLength - pAttrRecordHeader->Form.Nonresident.MappingPairsOffset;
    LONGLONG TotalBytes = 0;

    if (pAttrRecordHeader->Form.Nonresident.LowestVcn == 0LL)
    {
        FSRAttribInfo.ValidDataSize = pAttrRecordHeader->Form.Nonresident.ValidDataLength;
    }

    LONG BytesPerCluster = pVolReader->GetBytesPerCluster();

    //
    // now look at mapping table for the disk run's
    //
    while (PairDataLen > 0)
    {
        // 4.1. If it is 0, then break,
        if (pPairs[0] == 0)
        {
            // lengths == 0
            break;
        }

        // 4.2. else extract v and l (see above).
        ULONG countVCN = pPairs[0] & 0x0f;
        ULONG countLCN = (pPairs[0] >> 4) & 0x0f;
        ULONG countRecord = countVCN + countLCN + 1;

        if (countRecord > PairDataLen)
        {
            // bad record
            log::Error(pVolReader->GetLogger(), HRESULT_FROM_WIN32(GetLastError()), L"Got a bad VCN/LCN record!\r\n");
            break;
        }

        LONGLONG NextVcn = 0;
        LONGLONG ClusterCount = 0;
        LONGLONG DeltaLcn = 0;

        //  5.  Interpret the next v bytes as a signed quantity,
        //      with the low-order byte coming first.  Unpack it
        //      sign-extended into 64 bits and add it to NextVcn.
        //      (It can really only be positive, but the Lcn
        //      change can be positive or negative.)
        //
        // decode vcn & lcn delta's
        //
        // NextVcn is never negative - For little-endian RtlCopy will work
        RtlCopyMemory(&ClusterCount, &pPairs[1], countVCN);

        // DeltaLcn can be negative - check the last byte and set to 0xFF....
        if (IsCharLtrZero(pPairs[countVCN + countLCN]))
        {
            DeltaLcn = DeltaLcn - 1;
        }
        RtlCopyMemory(&DeltaLcn, &pPairs[1 + countVCN], countLCN);

        //  6.  Interpret the next l bytes as a signed quantity,
        //      with the low-order byte coming first.  Unpack it
        //      sign-extended into 64 bits and add it to
        //      CurrentLcn.  Remember, if this produces a
        //      CurrentLcn of 0, then the Vcns from the
        //      CurrentVcn to NextVcn-1 are unallocated.

        NextVcn = CurrentVcn + ClusterCount;
        CurrentLcn += DeltaLcn;
        //
        // If Current LCN becomes zero, it means that everything from CurrentVcn to NextVcn-1
        // is unallocated, so pad it up with zero's
        //
        if (0 == CurrentLcn || DeltaLcn == 0)
        {

            TotalBytes += ClusterCount * pVolReader->GetBytesPerFRS();

            FSRAttribInfo.ExtentsVector.push_back(NonResidentAttributeExtent(
                0, ClusterCount * BytesPerCluster, ClusterCount * BytesPerCluster, CurrentVcn, true));
        }
        else
        {

            //
            // the run is from CurrentVcn to NextVcn mapping to CurrentLcn
            //
            // DiskOffset = CurrentLcn * m_BytesPerCluster;
            // DiskRun = (NextVcn-CurrentVcn) * m_BytesPerCluster;

            //  7.  Update cached mapping information from
            //      CurrentVcn, NextVcn and CurrentLcn.
            TotalBytes += ClusterCount * BytesPerCluster;
            FSRAttribInfo.ExtentsVector.push_back(NonResidentAttributeExtent(
                (CurrentLcn * BytesPerCluster),
                ClusterCount * BytesPerCluster,
                ClusterCount * BytesPerCluster,
                CurrentVcn));
        }

        //  3.  CurrentVcn = NextVcn;
        CurrentVcn = NextVcn;
        PairDataLen -= countRecord;

        //  4.  Read next byte from stream.
        pPairs += countRecord;
    }

    FSRAttribInfo.ExtentsSize += TotalBytes;

    _ASSERT(pAttrRecordHeader->Form.Nonresident.HighestVcn == (CurrentVcn - 1));

    return S_OK;
}

HRESULT MFTUtils::GetDataSegments(
    MFTUtils::NonResidentDataAttrInfo& FSRAttribInfo,
    std::vector<MFTUtils::DataSegment>& ListOfSegments)
{
    // This offset represent the file based offset of the current segment
    ULONGLONG ullCurrentPosition = 0;
    ListOfSegments.reserve(FSRAttribInfo.ExtentsVector.size());
    for (auto iter = FSRAttribInfo.ExtentsVector.begin(); iter != FSRAttribInfo.ExtentsVector.end(); ++iter)
    {
        const NonResidentAttributeExtent& NRAE = *iter;

        if ((ullCurrentPosition + NRAE.DataSize) > FSRAttribInfo.ValidDataSize)
        {
            // This block contains data that is beyond the ValidDataSize limit
            if (ullCurrentPosition <= FSRAttribInfo.ValidDataSize)
            {
                // This block has to be split in two

                ULONGLONG ullValidData = FSRAttribInfo.ValidDataSize - ullCurrentPosition;

                if (ullValidData > 0LL)
                {

                    DataSegment datasegref_valid;

                    datasegref_valid.ullAllocatedSize = ullValidData;
                    datasegref_valid.ullSize = ullValidData;
                    datasegref_valid.ullDiskBasedOffset = NRAE.DiskOffset;
                    datasegref_valid.ullFileBasedOffset = ullCurrentPosition;
                    datasegref_valid.bUnallocated = NRAE.bZero;
                    datasegref_valid.bValidData = true;
                    ListOfSegments.push_back(datasegref_valid);
                }

                if (FSRAttribInfo.ValidDataSize < ullCurrentPosition + NRAE.DataSize)
                {

                    DataSegment datasegref_invalid;

                    datasegref_invalid.ullAllocatedSize = NRAE.DiskAlloc - ullValidData;
                    datasegref_invalid.ullSize = ullValidData < NRAE.DataSize ? NRAE.DataSize - ullValidData : 0LL;
                    datasegref_invalid.ullDiskBasedOffset = NRAE.DiskOffset + ullValidData;
                    datasegref_invalid.ullFileBasedOffset = ullCurrentPosition + ullValidData;
                    datasegref_invalid.bUnallocated = NRAE.bZero;
                    datasegref_invalid.bValidData = false;
                    ListOfSegments.push_back(datasegref_invalid);
                }
            }
            else
            {
                // This block is entirely invalid
                DataSegment datasegref;

                datasegref.ullAllocatedSize = NRAE.DiskAlloc;
                datasegref.ullSize = NRAE.DataSize;
                datasegref.ullDiskBasedOffset = NRAE.DiskOffset;
                datasegref.ullFileBasedOffset = ullCurrentPosition;
                datasegref.bUnallocated = NRAE.bZero;
                datasegref.bValidData = false;
                // Adding this segment
                ListOfSegments.push_back(datasegref);
            }
        }
        else
        {
            DataSegment datasegref;

            datasegref.ullAllocatedSize = NRAE.DiskAlloc;
            datasegref.ullSize = NRAE.DataSize;
            datasegref.ullDiskBasedOffset = NRAE.DiskOffset;
            datasegref.ullFileBasedOffset = ullCurrentPosition;
            datasegref.bUnallocated = NRAE.bZero;
            datasegref.bValidData = true;
            // Adding this segment
            ListOfSegments.push_back(datasegref);
        }
        ullCurrentPosition += NRAE.DataSize;
    }
    return S_OK;
}

HRESULT MFTUtils::GetDataSegmentsToRead(
    NonResidentDataAttrInfo& FSRAttribInfo,
    ULONGLONG ullStartOffset,
    ULONGLONG ullBytesToRead,
    std::vector<DataSegment>& ListOfSegments,
    ULONGLONG ullBlockSize)
{
    // This offset represent de file based offset of the current segment
    ULONGLONG ullSegmentFileOffset = 0;
    ULONGLONG ullCurrentPosition = 0;
    ULONGLONG ullDataLength = 0;

    for (auto iter = FSRAttribInfo.ExtentsVector.begin(); iter != FSRAttribInfo.ExtentsVector.end(); ++iter)
    {
        const NonResidentAttributeExtent& NRAE = *iter;

        const ULONGLONG ullSegmentEnd = NRAE.DiskOffset + NRAE.DataSize;
        const ULONGLONG ullSegmentStart = NRAE.DiskOffset;
        const ULONGLONG ullSegmentLength = ullSegmentEnd - ullSegmentStart;

        if ((ullSegmentFileOffset <= ullStartOffset + ullBytesToRead)
            && (ullSegmentFileOffset + ullSegmentLength > ullStartOffset))
        {
            // This segment is inside the 'window' of interest

            ULONGLONG ullInSegmentPosition = 0;

            if (ullStartOffset > ullSegmentFileOffset && ullStartOffset < ullSegmentFileOffset + ullSegmentLength)
            {
                // Window starts within this window, let's move to this position
                ullInSegmentPosition = ullStartOffset - ullSegmentFileOffset;
                ullCurrentPosition += ullInSegmentPosition;
            }
            else
            {
                // Window started before this segment, we read from the start of the segment
                ullInSegmentPosition = 0;
            }

            while (ullInSegmentPosition < ullSegmentLength && ullDataLength < ullBytesToRead)
            {
                // what can we read?
                ULONGLONG ullBytesStillNeeded = ullBytesToRead - ullDataLength;

                ULONGLONG ullToReadNow = 0;

                // we dont want to read more that what is needed
                ullToReadNow = std::min(ullBlockSize, ullBytesStillNeeded);

                // we don't want to read more that what is left in the segment
                ullToReadNow = std::min(ullToReadNow, ullSegmentLength - ullInSegmentPosition);

                {
                    DataSegment dataseg;

                    dataseg.ullSize = ullToReadNow;
                    dataseg.ullDiskBasedOffset = ullSegmentStart + ullInSegmentPosition;
                    dataseg.ullFileBasedOffset = ullCurrentPosition;
                    dataseg.bUnallocated = NRAE.bZero;

                    // Adding this segment
                    ListOfSegments.push_back(dataseg);

                    ullDataLength += ullToReadNow;
                    ullInSegmentPosition += ullToReadNow;
                    ullCurrentPosition += ullToReadNow;
                }
            }
        }
        else
        {
            ullCurrentPosition += ullSegmentLength;
        }
        ullSegmentFileOffset += ullSegmentLength;
    }
    return S_OK;
}

HRESULT MFTUtils::MultiSectorFixup(PFILE_RECORD_SEGMENT_HEADER pFRS, const std::shared_ptr<VolumeReader>& pVolReader)
{
    HRESULT hr = E_FAIL;
    WORD fixupsig, i;
    BYTE* dest;
    WORD numfix;
    const WORD* fixuparray;
    //
    // make sure it is a mft record
    //
    PMULTI_SECTOR_HEADER pHeader = (PMULTI_SECTOR_HEADER)pFRS;
    if (strncmp((PCHAR)pHeader->Signature, "FILE", 4))
    {
        if (strncmp((PCHAR)pHeader->Signature, "BAAD", 4) != 0)
        {
            _ASSERT(false);
        }
        log::Error(
            pVolReader->GetLogger(),
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            L"FILE Signature doesn't match in Fixup\r\n");
        return hr;
    }

    LONG lBytesPerSector = pVolReader->GetBytesPerSector();

    //
    // get the first fixup entry
    //
    fixuparray = (WORD*)((BYTE*)pHeader + pHeader->UpdateSequenceArrayOffset) + 1;
    fixupsig = fixuparray[-1];
    dest = (BYTE*)pHeader + lBytesPerSector - 2;
    numfix = (WORD)(pVolReader->GetBytesPerFRS() / lBytesPerSector);

    //
    // go through the fixups
    //
    for (i = 0; i < numfix; i++)
    {

        if (*((WORD*)dest) != fixupsig)
        {
            log::Error(
                pVolReader->GetLogger(),
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                L"FILE Fixup %hd does not match signature %hd\r\n",
                *((WORD*)dest),
                fixupsig);
            return hr;
        }
        *(WORD*)dest = fixuparray[i];
        dest += lBytesPerSector;
    }

    return S_OK;
}

HRESULT MFTUtils::MultiSectorFixup(
    PINDEX_ALLOCATION_BUFFER pFRS,
    DWORD dwSizeOfIndex,
    const std::shared_ptr<VolumeReader>& pVolReader)
{
    WORD fixupsig, i;
    BYTE* dest;
    WORD numfix;
    const WORD* fixuparray;
    //
    // make sure its an mft record
    //
    PMULTI_SECTOR_HEADER pHeader = &pFRS->MultiSectorHeader;
    if (strncmp((PCHAR)pHeader->Signature, "INDX", 4))
    {
        if (!memcmp((PCHAR)pHeader->Signature, "\0\0\0\0", 4))
        {
            log::Verbose(
                pVolReader->GetLogger(),
                L"Failed to parse $INDEX_ALLOCATION header : invalid block (uninitialized)\r\n");
        }
        else
        {
            log::Verbose(
                pVolReader->GetLogger(),
                L"Failed to parse $INDEX_ALLOCATION header (%C%C%C%C)\r\n",
                (CHAR)pHeader->Signature[0],
                (CHAR)pHeader->Signature[1],
                (CHAR)pHeader->Signature[2],
                (CHAR)pHeader->Signature[3]);
        }
        return HRESULT_FROM_NT(NTE_BAD_SIGNATURE);
    }

    LONG lBytesPerSector = pVolReader->GetBytesPerSector();

    //
    // get the first fixup entry
    //
    fixuparray = (WORD*)((BYTE*)pHeader + pHeader->UpdateSequenceArrayOffset) + 1;
    fixupsig = fixuparray[-1];
    dest = (BYTE*)pHeader + lBytesPerSector - 2;
    numfix = (WORD)(dwSizeOfIndex / lBytesPerSector);

    //
    // go through the fixups
    //
    for (i = 0; i < numfix; i++)
    {

        if (*((WORD*)dest) != fixupsig)
        {
            log::Info(
                pVolReader->GetLogger(), L"INDX Fixup %hd does not match signature %hd\r\n", *((WORD*)dest), fixupsig);
            return E_FAIL;
        }
        *(WORD*)dest = fixuparray[i];
        dest += lBytesPerSector;
    }

    return S_OK;
}