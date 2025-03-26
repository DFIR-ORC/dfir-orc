//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ArchiveExtract.h"

#pragma managed(push, off)

namespace Orc {

class ZipExtract : public ArchiveExtract
{
    friend class ArchiveExtract;

public:
    ZipExtract(bool bComputeHash = false);

    STDMETHOD(Extract)
    (__in MakeArchiveStream makeArchiveStream,
     __in const ItemShouldBeExtractedCallback pShouldBeExtracted,
     __in MakeOutputStream MakeWriteAbleStream);

    ~ZipExtract(void);
};

}  // namespace Orc

#pragma managed(pop)
