//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "DataDetails.h"
#include "FSUtils.h"

#pragma managed(push, off)

namespace Orc {

class FileInfo;

class PEInfo
{
public:
    PEInfo(FileInfo& fileInfo);
    ~PEInfo();

    bool HasFileVersionInfo();
    bool HasExeHeader();
    bool HasPEHeader();

    const IMAGE_DOS_HEADER* GetImageDosHeader() const;
    const IMAGE_FILE_HEADER* GetImageFileHeader() const;
    const IMAGE_OPTIONAL_HEADER32* GetPe32OptionalHeader() const;
    const IMAGE_OPTIONAL_HEADER64* GetPe64OptionalHeader() const;

    HRESULT CheckPEInformation();
    HRESULT OpenPEInformation();

    HRESULT CheckVersionInformation();
    HRESULT OpenVersionInformation();

    HRESULT CheckSecurityDirectory();
    HRESULT OpenSecurityDirectory();

    HRESULT OpenPeHash(Intentions localIntentions);
    HRESULT OpenAllHash(Intentions localIntentions);

private:
    FileInfo& m_FileInfo;
};
}  // namespace Orc

#pragma managed(pop)
