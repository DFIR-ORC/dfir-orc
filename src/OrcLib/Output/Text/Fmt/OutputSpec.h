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

#include "Output/Text/Format.h"
#include <OutputSpec.h>

template <>
struct fmt::formatter<Orc::OutputSpec::UploadAuthScheme> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadAuthScheme& scheme, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(scheme);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadAuthScheme, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadAuthScheme& scheme, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(scheme);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadMethod> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadMethod& method, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(method);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadMethod, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadMethod& method, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(method);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadOperation> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadOperation& operation, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(operation);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadOperation, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadOperation& operation, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(operation);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadMode> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadMode& mode, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(mode);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::UploadMode, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::UploadMode& mode, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(mode);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::Encoding> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::Encoding& encoding, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(encoding);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::Encoding, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::Encoding& encoding, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(encoding);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::Kind> : public fmt::formatter<fmt::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::Kind& kind, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(kind);

        std::error_code ec;
        const auto utf8 = Utf16ToUtf8(utf16, ec);
        if (ec)
        {
            return fmt::format_to(ctx.out(), "<failed_conversion>");
        }

        std::copy(std::cbegin(utf8), std::cend(utf8), ctx.out());
        return ctx.out();
    }
};

template <>
struct fmt::formatter<Orc::OutputSpec::Kind, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpec::Kind& kind, FormatContext& ctx)
    {
        const auto utf16 = Orc::OutputSpec::ToString(kind);
        std::copy(std::cbegin(utf16), std::cend(utf16), ctx.out());
        return ctx.out();
    }
};
