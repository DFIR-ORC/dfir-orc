//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
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

    PIMAGE_NT_HEADERS32 GetPe32Header() const;
    PIMAGE_NT_HEADERS64 GetPe64Header() const;
    PIMAGE_SECTION_HEADER GetPeSections() const;

    size_t PeRvaToFileOffset(size_t RvaStart, size_t Length, PIMAGE_SECTION_HEADER Sections, size_t SectionCount);

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
