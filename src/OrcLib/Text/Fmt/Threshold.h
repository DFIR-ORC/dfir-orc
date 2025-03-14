//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>

#include "Utils/Threshold.h"

template <typename T>
struct fmt::formatter<Orc::Threshold<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Threshold<T>& threshold, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::string_view>::format(threshold.ToString(), ctx);
    }
};

template <typename T>
struct fmt::formatter<Orc::Threshold<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Threshold<T>& threshold, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(threshold.ToWString(), ctx);
    }
};
