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

#pragma managed(push, off)

namespace Orc {

class OfflineMFTReader;

class MFTOffline : public IMFT
{
public:
    MFTOffline(std::shared_ptr<OfflineMFTReader>& volReader);
    virtual ~MFTOffline();

    // from IMFT
    virtual HRESULT Initialize();

    virtual ULONG64 GetMftOffset() { return 0LL; }

    virtual HRESULT EnumMFTRecord(MFTUtils::EnumMFTRecordCall pCallBack);
    virtual HRESULT FetchMFTRecord(std::vector<MFT_SEGMENT_REFERENCE>& frn, MFTUtils::EnumMFTRecordCall pCallBack);
    virtual ULONG GetMFTRecordCount() const;
    virtual MFTUtils::SafeMFTSegmentNumber GetUSNRoot() const;

private:
    std::shared_ptr<OfflineMFTReader> m_pVolReader;
    std::shared_ptr<OfflineMFTReader> m_pFetchReader;

    MFTUtils::SafeMFTSegmentNumber m_RootUSN;
};
}  // namespace Orc

#pragma managed(pop)
