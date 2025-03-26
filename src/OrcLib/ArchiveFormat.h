//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma managed(push, off)

#include "Archive/IArchive.h"

namespace Orc {

enum ArchiveFormat
{
    Unknown = 0,
    SevenZip,
    Zip,
    SevenZipSupported,
};

Archive::Format ToArchiveFormatNg(ArchiveFormat legacyFormat);

}  // namespace Orc
#pragma managed(pop)
