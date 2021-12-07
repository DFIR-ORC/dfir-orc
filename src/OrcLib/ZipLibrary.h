//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>

#pragma managed(push, off)

namespace Orc {

class ZipLibrary
{
public:
    class ArchiveFormat
    {
    public:
        GUID ID;
        std::wstring Name;
        std::vector<std::wstring> Extensions;
        bool UpdateCapable;

        ArchiveFormat()
            : ID {0}
            , UpdateCapable(false)
        {
        }
        ArchiveFormat(ArchiveFormat&& other) = default;
        ArchiveFormat(const ArchiveFormat&) = default;
    };

    class ArchiveCodec
    {
    public:
        GUID ID;
        std::wstring Name;
    };

    static std::unique_ptr<ZipLibrary> CreateZipLibrary();
    static std::shared_ptr<ZipLibrary> GetZipLibrary();

    ~ZipLibrary();

    HRESULT CreateObject(const GUID* clsID, const GUID* iid, void** outObject);

    const std::vector<ArchiveFormat>& Formats() const { return m_Formats; };

    const GUID GetFormatCLSID(const std::wstring& anExt) const;

private:
    ZipLibrary() = default;
    HRESULT Initialize();
    HRESULT GetAvailableFormats(std::vector<ArchiveFormat>& formats) const;
    HRESULT GetAvailableCodecs(std::vector<ArchiveCodec>& codecs) const;

    std::vector<ArchiveFormat> m_Formats;
    std::vector<ArchiveCodec> m_Codecs;
};
}  // namespace Orc

#pragma managed(pop)
