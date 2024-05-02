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

#include "Text/Guid.h"

template <>
struct fmt::formatter<GUID> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const GUID& guid, FormatContext& ctx) const
    {
        fmt::basic_memory_buffer<char, Orc::kGuidStringLength> s;
        Orc::ToString(guid, std::back_inserter(s));
        return formatter<std::string_view>::format(std::string_view(s.data(), s.size()), ctx);
    }
};

template <>
struct fmt::formatter<GUID, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const GUID& guid, FormatContext& ctx) const
    {
        fmt::basic_memory_buffer<wchar_t, Orc::kGuidStringLength> s;
        Orc::ToString(guid, std::back_inserter(s));
        return formatter<std::wstring_view, wchar_t>::format(std::wstring_view(s.data(), s.size()), ctx);
    }
};
