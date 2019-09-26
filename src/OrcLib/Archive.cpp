//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <memory>
#include <filesystem>

#include "Archive.h"

#include "LogFileWriter.h"

#include "ZipLibrary.h"

#include "CaseInsensitive.h"

namespace fs = std::filesystem;
using namespace Orc;

Orc::ArchiveFormat Archive::GetArchiveFormat(const std::wstring_view& filepath)
{
    auto path = fs::path(std::wstring(filepath));

    std::wstring ext = path.extension();

    if (ext.empty())
        ext = path.stem();

    if (ext.empty())
        return ArchiveFormat::Unknown;

    if (equalCaseInsensitive(ext, L".cab"))
        return ArchiveFormat::Cabinet;
    if (equalCaseInsensitive(ext, L".zip"))
        return ArchiveFormat::Zip;
    if (equalCaseInsensitive(ext, L".7z"))
        return ArchiveFormat::SevenZip;
    if (equalCaseInsensitive(ext, L"cab"))
        return ArchiveFormat::Cabinet;
    if (equalCaseInsensitive(ext, L"zip"))
        return ArchiveFormat::Zip;
    if (equalCaseInsensitive(ext, L"7z"))
        return ArchiveFormat::SevenZip;

    const auto pZipLib = ZipLibrary::CreateZipLibrary(nullptr);
    if (pZipLib == nullptr)
        return ArchiveFormat::Unknown;

    for (const auto& format : pZipLib->Formats())
    {
        for (const auto& fmtext : format.Extensions)
        {
            if (equalCaseInsensitive(ext, fmtext))
                return ArchiveFormat::SevenZipSupported;
        }
    }

    return ArchiveFormat::Unknown;
}

std::wstring_view Archive::GetArchiveFormatString(Orc::ArchiveFormat format)
{
    using namespace std::string_view_literals;
    switch (format)
    {
        case ArchiveFormat::Cabinet:
            return L"cab"sv;
        case ArchiveFormat::Zip:
            return L"zip"sv;
        case ArchiveFormat::SevenZip:
            return L"7z"sv;
        default:
            return L"Unknown";
    }
}

HRESULT Archive::SetCallback(Archive::ArchiveCallback aCallback)
{
    m_Callback = aCallback;
    return S_OK;
}

const Archive::ArchiveItem& Archive::operator[](const std::wstring& strNameInArchive)
{
    static const ArchiveItem g_NotFound;
    const auto& found = std::find_if(begin(m_Items), end(m_Items), [strNameInArchive](const ArchiveItem& item) -> bool {
        return equalCaseInsensitive(item.NameInArchive, strNameInArchive);
    });
    return found == end(m_Items) ? g_NotFound : *found;
}

Archive::~Archive(void) {}
