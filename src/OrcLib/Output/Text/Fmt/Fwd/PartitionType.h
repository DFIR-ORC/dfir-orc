//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>

namespace Orc {

enum class PartitionType;

}

template <>
struct fmt::formatter<Orc::PartitionType> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::PartitionType& type, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::PartitionType, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::PartitionType& type, FormatContext& ctx) -> decltype(ctx.out());
};
