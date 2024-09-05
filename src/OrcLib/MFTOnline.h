//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "IMFT.h"
#include "VolumeReader.h"

#pragma managed(push, off)

namespace Orc {

class MFTRecord;

class MFTOnline : public IMFT
{
public:
    MFTOnline(std::shared_ptr<VolumeReader> volReader);
    virtual ~MFTOnline();

    // from IMFT
    virtual HRESULT Initialize();

    virtual ULONG64 GetMftOffset() { return m_MftOffset; }

    virtual HRESULT EnumMFTRecord(MFTUtils::EnumMFTRecordCall pCallBack);
    virtual HRESULT FetchMFTRecord(std::vector<MFT_SEGMENT_REFERENCE>& frn, MFTUtils::EnumMFTRecordCall pCallBack);
    virtual ULONG GetMFTRecordCount() const;
    virtual MFTUtils::SafeMFTSegmentNumber GetUSNRoot() const { return m_RootUSN; }

    const MFTUtils::NonResidentDataAttrInfo& GetMftInfo() const { return m_MFT0Info; }

private:
    std::shared_ptr<VolumeReader> m_pVolReader;
    std::shared_ptr<VolumeReader> m_pFetchReader;

    HRESULT GetMFTExtents(const CBinaryBuffer& buffer);

    HRESULT GetMFTChildsRecordExtents(
        const std::shared_ptr<VolumeReader>& volume,
        MFTRecord& mftRecord,
        MFTUtils::NonResidentDataAttrInfo& extentsInfo);

    HRESULT
    FetchMFTRecord(MFT_SEGMENT_REFERENCE& frn, MFTUtils::EnumMFTRecordCall pCallBack, bool& hasFoundRecord);

    ULONG64 m_MftOffset;
    MFTUtils::NonResidentDataAttrInfo m_MFT0Info;

    MFTUtils::SafeMFTSegmentNumber m_RootUSN;
};
}  // namespace Orc

#pragma managed(pop)
