//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "stdafx.h"

#include "OrcLib.h"

#include "ByteStream.h"

#include "ArchiveExtract.h"

#include <string>
#include <memory>

#include <fdi.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API CabExtract : public ArchiveExtract
{
    friend class ArchiveExtract;

public:

    CabExtract(logger pLog, bool bComputeHash = false);

	STDMETHOD(Extract)
    (__in MakeArchiveStream makeArchiveStream,
     __in const ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted,
     __in MakeOutputStream MakeWriteAbleStream);

    virtual ~CabExtract();

private:

	typedef struct _DECOMP_FILE_HANDLE
    {
        CabExtract* pOwner;
        Archive::ArchiveItem Item;
    } DECOMP_FILE_HANDLE, *PDECOMP_FILE_HANDLE;

    HRESULT GetHRESULTFromERF(__in ERF& erf);

    bool IsCancelled();

    static FNALLOC(static_FDIAlloc);
    static FNFREE(static_FDIFree);
    static FNOPEN(static_FDIOpen);
    static FNREAD(static_FDIRead);
    static FNWRITE(static_FDIWrite);
    static FNCLOSE(static_FDIClose);
    static FNSEEK(static_FDISeek);
    static FNFDINOTIFY(static_FDINotify);
    FNFDINOTIFY(FDINotify);

    ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted = nullptr;

};
}  // namespace Orc

#pragma managed(pop)
