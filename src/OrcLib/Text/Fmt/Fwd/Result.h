//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>
#include <boost/outcome/outcome.hpp>

template <typename T>
struct fmt::formatter<boost::outcome_v2::std_result<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const boost::outcome_v2::std_result<T>& result, FormatContext& ctx) -> decltype(ctx.out());
};

template <typename T>
struct fmt::formatter<boost::outcome_v2::std_result<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const boost::outcome_v2::std_result<T>& result, FormatContext& ctx) -> decltype(ctx.out());
};
