//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <windows.h>

#include <fmt/format.h>

#include "Output/Text/Fmt/Fwd/OutputSpecTypes.h"

#include "Output/Text/Format.h"
#include <OutputSpec.h>

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadAuthScheme>::format(
    const Orc::OutputSpecTypes::UploadAuthScheme& scheme,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(scheme), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadAuthScheme, wchar_t>::format(
    const Orc::OutputSpecTypes::UploadAuthScheme& scheme,
    FormatContext& ctx) -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(scheme), ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadMethod>::format(
    const Orc::OutputSpecTypes::UploadMethod& method,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(method), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadMethod, wchar_t>::format(
    const Orc::OutputSpecTypes::UploadMethod& method,
    FormatContext& ctx) -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(method), ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadOperation>::format(
    const Orc::OutputSpecTypes::UploadOperation& operation,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(operation), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadOperation, wchar_t>::format(
    const Orc::OutputSpecTypes::UploadOperation& operation,
    FormatContext& ctx) -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(operation), ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadMode>::format(
    const Orc::OutputSpecTypes::UploadMode& mode,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(mode), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::UploadMode, wchar_t>::format(
    const Orc::OutputSpecTypes::UploadMode& mode,
    FormatContext& ctx) -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(mode), ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::Encoding>::format(
    const Orc::OutputSpecTypes::Encoding& encoding,
    FormatContext& ctx) -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(encoding), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::Encoding, wchar_t>::format(
    const Orc::OutputSpecTypes::Encoding& encoding,
    FormatContext& ctx) -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(encoding), ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::Kind>::format(const Orc::OutputSpecTypes::Kind& kind, FormatContext& ctx)
    -> decltype(ctx.out())
{
    std::error_code ec;
    const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(kind), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(utf8, ctx);
}

template <typename FormatContext>
auto fmt::formatter<Orc::OutputSpecTypes::Kind, wchar_t>::format(
    const Orc::OutputSpecTypes::Kind& kind,
    FormatContext& ctx) -> decltype(ctx.out())
{
    return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(kind), ctx);
}
