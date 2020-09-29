//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>

#include <guiddef.h>

#include <fmt/format.h>

template <>
struct fmt::formatter<GUID> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const GUID& guid, FormatContext& ctx)
    {
        const auto s = fmt::format("{}-{}-{}-{}", guid.Data1, guid.Data2, guid.Data3, guid.Data4);
        return formatter<std::string_view>::format(s, ctx);
    }
};

template <>
struct fmt::formatter<GUID, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const GUID& guid, FormatContext& ctx)
    {
        const auto s = fmt::format(L"{}-{}-{}-{}", guid.Data1, guid.Data2, guid.Data3, guid.Data4);
        return formatter<std::wstring_view, wchar_t>::format(s, ctx);
    }
};
