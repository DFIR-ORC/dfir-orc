//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <FuzzyHashStreamAlgorithm.h>

#include "Text/Iconv.h"

template <>
struct fmt::formatter<Orc::FuzzyHashStreamAlgorithm> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::FuzzyHashStreamAlgorithm& algs, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (algs == Orc::FuzzyHashStreamAlgorithm::Undefined)
        {
            return formatter<std::string_view>::format("None", ctx);
        }

        std::error_code ec;
        const auto algorithm = Orc::ToUtf8(Orc::FuzzyHashStream::GetSupportedAlgorithm(algs), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(algorithm, ctx);
    }
};

template <>
struct fmt::formatter<Orc::FuzzyHashStreamAlgorithm, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::FuzzyHashStreamAlgorithm& algs, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (algs == Orc::FuzzyHashStreamAlgorithm::Undefined)
        {
            return formatter<std::wstring_view, wchar_t>::format(L"None", ctx);
        }

        const auto algorithm = Orc::FuzzyHashStream::GetSupportedAlgorithm(algs);
        return formatter<std::wstring_view, wchar_t>::format(algorithm, ctx);
    }
};
