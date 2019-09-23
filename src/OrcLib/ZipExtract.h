//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ArchiveExtract.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ZipExtract : public ArchiveExtract
{
    friend class ArchiveExtract;
    friend class std::_Ref_count_obj<ZipExtract>;

private:
    ZipExtract(logger pLog, bool bComputeHash = false);

public:
    STDMETHOD(Extract)
    (__in MakeArchiveStream makeArchiveStream,
     __in const ItemShouldBeExtractedCallback pShouldBeExtracted,
     __in MakeOutputStream MakeWriteAbleStream);

    ~ZipExtract(void);
};

}  // namespace Orc

#pragma managed(pop)
