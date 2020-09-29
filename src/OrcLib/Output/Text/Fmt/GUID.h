//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <guiddef.h>

#include <fmt/format.h>

template <>
struct fmt::formatter<GUID> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const GUID& guid, FormatContext& ctx)
    {
        fmt::format_to(ctx.out(), "{}-{}-{}-{}", guid.Data1, guid.Data2, guid.Data3, guid.Data4);
        return ctx.out();
    }
};

template <>
struct fmt::formatter<GUID, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const GUID& type, FormatContext& ctx)
    {
        fmt::format_to(ctx.out(), L"{}-{}-{}-{}", guid.Data1, guid.Data2, guid.Data3, guid.Data4);
        return ctx.out();
    }
};
