//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <vector>
#include <functional>

#include "OrcLib.h"

#include "BinaryBuffer.h"
#include "ByteStream.h"

#include "NtfsDataStructures.h"
#include "Utils/TypeTraits.h"

#pragma managed(push, off)

namespace Orc {

class VolumeReader;

class MFTUtils
{

public:
    class DataSegment
    {
    public:
        ULONGLONG ullDiskBasedOffset;
        ULONGLONG ullFileBasedOffset;
        ULONGLONG ullSize;
        ULONGLONG ullAllocatedSize;
        bool bUnallocated;
        bool bValidData;
    };

    class NonResidentAttributeExtent
    {
    public:
        ULONGLONG DiskOffset;
        ULONGLONG DiskAlloc;
        ULONGLONG DataSize;
        ULONGLONG LowestVCN;
        bool bZero;

        NonResidentAttributeExtent()
        {
            DiskOffset = 0LL;
            DiskAlloc = 0LL;
            DataSize = 0LL;
            LowestVCN = 0LL;
            bZero = false;
        };

        NonResidentAttributeExtent(ULONGLONG DOf, ULONGLONG DEx, ULONGLONG DData, ULONGLONG Dvcn, bool bZ = false)
        {
            DiskOffset = DOf;
            DiskAlloc = DEx;
            DataSize = DData;
            LowestVCN = Dvcn;
            bZero = bZ;
        };
    };

    typedef std::vector<NonResidentAttributeExtent> NonResidentAttributeExtentVector;

    class NonResidentDataAttrInfo
    {
    public:
        NonResidentAttributeExtentVector ExtentsVector;  // holds the extent of the attribute

        ULONGLONG ExtentsSize;  // total size of the extents
        ULONGLONG DataSize;  // total size of the data (extents are rounded to cluster size,
        ULONGLONG ValidDataSize;
        // we need to know the real datasize.
        NonResidentDataAttrInfo()
        {
            ExtentsSize = 0LL;
            DataSize = 0LL;
            ValidDataSize = 0LL;
        };
    };

    typedef ULONG UnSafeMFTSegmentNumber;
    typedef ULONGLONG SafeMFTSegmentNumber;

    typedef std::function<HRESULT(SafeMFTSegmentNumber& ulRecordIndex, CBinaryBuffer& Data)> EnumMFTRecordCall;

    static HRESULT GetAttributeNRExtents(
        PATTRIBUTE_RECORD_HEADER pRecord,
        NonResidentDataAttrInfo& FSRAttribInfo,
        const std::shared_ptr<VolumeReader>& pVolReader);
    static HRESULT GetDataSegments(NonResidentDataAttrInfo& FSRAttribInfo, std::vector<DataSegment>& ListOfSegments);
    static HRESULT GetDataSegmentsToRead(
        NonResidentDataAttrInfo& FSRAttribInfo,
        ULONGLONG ullStartOffset,
        ULONGLONG ullBytesToRead,
        std::vector<DataSegment>& ListOfSegments,
        ULONGLONG ullBlockSize = DEFAULT_READ_SIZE);
    static HRESULT MultiSectorFixup(PFILE_RECORD_SEGMENT_HEADER pFRS, const std::shared_ptr<VolumeReader>& pVolReader);
    static HRESULT MultiSectorFixup(
        PINDEX_ALLOCATION_BUFFER pFRS,
        DWORD dwSizeOfIndex,
        const std::shared_ptr<VolumeReader>& pVolReader);
};

}  // namespace Orc

#pragma managed(pop)
