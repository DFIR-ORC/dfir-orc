//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "FileDisposition.h"

#include <string>

namespace Orc {

constexpr std::wstring_view kAppend = L"append";
constexpr std::wstring_view kTruncate = L"truncate";
constexpr std::wstring_view kCreateNew = L"create_new";
constexpr std::wstring_view kUnknown = L"unknown";

std::wstring_view ToString(FileDisposition disposition)
{
    switch (disposition)
    {
        case FileDisposition::Append:
            return kAppend;
        case FileDisposition::Truncate:
            return kTruncate;
        case FileDisposition::CreateNew:
            return kCreateNew;
        default:
            return kUnknown;
    }
}

Orc::Result<FileDisposition> ToFileDisposition(const std::wstring& disposition)
{
    const std::map<std::wstring_view, FileDisposition> map = {
        {kAppend, FileDisposition::Append},
        {kTruncate, FileDisposition::Truncate},
        {kCreateNew, FileDisposition::CreateNew}};

    auto it = map.find(disposition);
    if (it == std::cend(map))
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    return it->second;
}

std::ios_base::openmode ToOpenMode(FileDisposition disposition)
{
    switch (disposition)
    {
        case FileDisposition::Append:
            return std::ios::app;
        case FileDisposition::CreateNew:
        case FileDisposition::Truncate:
            return std::ios_base::trunc;
        default:
            assert(nullptr);
            return std::ios_base::ate;
    }
}

}  // namespace Orc
