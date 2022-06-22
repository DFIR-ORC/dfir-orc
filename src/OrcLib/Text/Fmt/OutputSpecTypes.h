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

#include <OutputSpec.h>

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadAuthScheme> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadAuthScheme& scheme, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(scheme), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadAuthScheme, wchar_t>
    : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadAuthScheme& scheme, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(scheme), ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMethod> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMethod& method, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(method), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMethod, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMethod& method, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(method), ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadOperation> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadOperation& operation, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(operation), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadOperation, wchar_t>
    : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadOperation& operation, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(operation), ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMode> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMode& mode, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(mode), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMode, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMode& mode, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(mode), ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Encoding> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Encoding& encoding, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(encoding), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Encoding, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Encoding& encoding, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(encoding), ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Kind> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Kind& kind, FormatContext& ctx) -> decltype(ctx.out())
    {
        std::error_code ec;
        const auto utf8 = Orc::Utf16ToUtf8(Orc::ToString(kind), ec);
        if (ec)
        {
            return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
        }

        return formatter<std::string_view>::format(utf8, ctx);
    }
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Kind, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Kind& kind, FormatContext& ctx) -> decltype(ctx.out())
    {
        return formatter<std::wstring_view, wchar_t>::format(Orc::ToString(kind), ctx);
    }
};
