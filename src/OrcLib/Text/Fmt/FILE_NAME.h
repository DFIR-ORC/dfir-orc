//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "NtfsDataStructures.h"

template <>
struct fmt::formatter<FILE_NAME> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const FILE_NAME& fileName, FormatContext& ctx) const -> decltype(ctx.out())
    {
        std::error_code ec;

        const auto name = Orc::ToUtf8(std::wstring_view(fileName.FileName, fileName.FileNameLength), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(name, ctx);
    }
};

template <>
struct fmt::formatter<FILE_NAME, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const FILE_NAME& fileName, FormatContext& ctx) const -> decltype(ctx.out())
    {
        const auto name = std::wstring_view(fileName.FileName, fileName.FileNameLength);
        return formatter<std::wstring_view, wchar_t>::format(name, ctx);
    }
};
