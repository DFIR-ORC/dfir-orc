//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <fmt/format.h>

#include "Output/Text/Format.h"
#include <Partition.h>
#include "Utils/Iconv.h"

template <>
struct fmt::formatter<Orc::Partition::Flags> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Partition::Flags& flags, FormatContext& ctx)
    {
        const auto utf16 = Orc::Partition::ToString(flags);

        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::Partition::Flags, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Partition::Flags& flags, FormatContext& ctx)
    {
        const auto s = Orc::Partition::ToString(flags);
        std::copy(std::cbegin(s), std::cend(s), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::Partition::Type> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Partition::Type& type, FormatContext& ctx)
    {
        const auto utf16 = Orc::Partition::ToString(type);

        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::Partition::Type, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Partition::Type& type, FormatContext& ctx)
    {
        const auto s = Orc::Partition::ToString(type);
        std::copy(std::cbegin(s), std::cend(s), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::Partition> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::Partition::Type& partition, FormatContext& ctx)
    {
        const auto utf16 = Orc::Partition::ToString(partition);

        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::Partition, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Partition& partition, FormatContext& ctx)
    {
        const auto s = Orc::Partition::ToString(partition);
        std::copy(std::cbegin(s), std::cend(s), ctx.out());
        return ctx.out();
    }
};
