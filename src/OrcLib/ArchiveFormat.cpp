//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "ArchiveFormat.h"

#include "Log/Log.h"

namespace Orc {

Archive::Format ToArchiveFormatNg(ArchiveFormat legacyFormat)
{
    switch (legacyFormat)
    {
        case ArchiveFormat::SevenZip:
        case ArchiveFormat::SevenZipSupported:
            return Archive::Format::k7z;
        case ArchiveFormat::Zip:
            return Archive::Format::k7zZip;
        default:
            Log::Error("Unexpected archive format, defaulting to 7z");
            return Archive::Format::k7z;
    }
}

}  // namespace Orc
