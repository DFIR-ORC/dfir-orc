#pragma once
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <filesystem>

#include <fmt/format.h>

template <>
struct fmt::formatter<std::filesystem::path> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx)
    {
        return fmt::formatter<std::string_view>::format(path.string(), ctx);
    }
};

template <>
struct fmt::formatter<std::filesystem::path, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx)
    {
        return fmt::formatter<std::wstring_view, wchar_t>::format(path.wstring(), ctx);
    }
};
