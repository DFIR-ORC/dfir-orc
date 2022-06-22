//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <CryptoHashStreamAlgorithm.h>

#include "Text/Iconv.h"

template <>
struct fmt::formatter<Orc::CryptoHashStreamAlgorithm> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::CryptoHashStreamAlgorithm& algs, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (algs == Orc::CryptoHashStream::Algorithm::Undefined)
        {
            return formatter<std::string_view>::format("None", ctx);
        }

        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::CryptoHashStream::GetSupportedAlgorithm(algs), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::CryptoHashStreamAlgorithm, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::CryptoHashStreamAlgorithm& algs, FormatContext& ctx) -> decltype(ctx.out())
    {
        if (algs == Orc::CryptoHashStream::Algorithm::Undefined)
        {
            return formatter<std::wstring_view, wchar_t>::format(L"None", ctx);
        }

        const auto algorithm = Orc::CryptoHashStream::GetSupportedAlgorithm(algs);
        return formatter<std::wstring_view, wchar_t>::format(algorithm, ctx);
    }
};
