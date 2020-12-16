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
namespace Traits {

template <typename T>
struct Offset;

}
}  // namespace Orc

template <typename T>
struct fmt::formatter<Orc::Traits::Offset<T>> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::Offset<T>& offset, FormatContext& ctx) -> decltype(ctx.out());
};

template <typename T>
struct fmt::formatter<Orc::Traits::Offset<T>, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Traits::Offset<T>& offset, FormatContext& ctx) -> decltype(ctx.out());
};
