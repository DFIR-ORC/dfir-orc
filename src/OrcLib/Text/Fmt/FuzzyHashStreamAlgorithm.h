//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Fmt/Fwd/FuzzyHashStreamAlgorithm.h"

#include <FuzzyHashStreamAlgorithm.h>

#include "Utils/Iconv.h"

template <typename FormatContext>
auto fmt::formatter<Orc::FuzzyHashStreamAlgorithm>::format(
    const Orc::FuzzyHashStreamAlgorithm& algs,
    FormatContext& ctx) -> decltype(ctx.out())
{
    if (algs == Orc::FuzzyHashStreamAlgorithm::Undefined)
    {
        return formatter<std::string_view>::format("None", ctx);
    }

    std::error_code ec;
    const auto algorithm = Orc::Utf16ToUtf8(Orc::FuzzyHashStream::GetSupportedAlgorithm(algs), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(algorithm, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::FuzzyHashStreamAlgorithm, wchar_t>::format(
    const Orc::FuzzyHashStreamAlgorithm& algs,
    FormatContext& ctx) -> decltype(ctx.out())
{
    if (algs == Orc::FuzzyHashStreamAlgorithm::Undefined)
    {
        return formatter<std::wstring_view, wchar_t>::format(L"None", ctx);
    }

    const auto algorithm = Orc::FuzzyHashStream::GetSupportedAlgorithm(algs);
    return formatter<std::wstring_view, wchar_t>::format(algorithm, ctx);
}
