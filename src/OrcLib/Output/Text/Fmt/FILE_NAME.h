//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>

#include "NtfsDataStructures.h"

template <>
struct fmt::formatter<FILE_NAME> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const FILE_NAME& fileName, FormatContext& ctx)
    {
        std::error_code ec;

        const auto name = Orc::Utf16ToUtf8(std::wstring_view(fileName.FileName, fileName.FileNameLength), ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(name), std::cend(name), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<FILE_NAME, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const FILE_NAME& fileName, FormatContext& ctx)
    {
        const auto name = std::wstring_view(fileName.FileName, fileName.FileNameLength);
        std::copy(std::cbegin(name), std::cend(name), ctx.out());
        return ctx.out();
    }
};
