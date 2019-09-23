//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "MFTUtils.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API IMFT
{
public:
    virtual ~IMFT() {}

    virtual HRESULT Initialize() PURE;

    virtual ULONG64 GetMftOffset() PURE;

    virtual HRESULT EnumMFTRecord(MFTUtils::EnumMFTRecordCall pCallBack) PURE;
    virtual HRESULT FetchMFTRecord(std::vector<MFT_SEGMENT_REFERENCE>& frn, MFTUtils::EnumMFTRecordCall pCallBack) PURE;

    virtual ULONG GetMFTRecordCount() const PURE;
    virtual MFTUtils::SafeMFTSegmentNumber GetUSNRoot() const PURE;
};  // IMFT

}  // namespace Orc

#pragma managed(pop)
