//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <fmt/format.h>

#include <FuzzyHashStream.h>

#include "Utils/Iconv.h"

template <>
struct fmt::formatter<Orc::FuzzyHashStream::Algorithm> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::FuzzyHashStream::Algorithm& algs, FormatContext& ctx)
    {
        if (algs == FuzzyHashStream::Algorithm::Undefined)
        {
            fmt::format_to(ctx.out(), "None");
            return ctx.out();
        }

        std::error_code ec;
        Orc::Utf16ToUtf8(fmt::format(Orc::FuzzyHashStream::GetSupportedAlgorithm(algs), ec));
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::FuzzyHashStream::Algorithm, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::FuzzyHashStream::Algorithm& algs, FormatContext& ctx)
    {
        if (algs == Orc::FuzzyHashStream::Algorithm::Undefined)
        {
            fmt::format_to(ctx.out(), L"None");
            return ctx.out();
        }

        return fmt::format_to(ctx.out(), Orc::FuzzyHashStream::GetSupportedAlgorithm(algs));
    }
};
