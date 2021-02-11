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
namespace OutputSpecTypes {

enum class UploadAuthScheme : char;
enum class UploadMethod : char;
enum class UploadOperation : char;
enum class UploadMode : char;
enum class Kind;
enum class Encoding;

}  // namespace OutputSpecTypes
}  // namespace Orc

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadAuthScheme> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadAuthScheme& scheme, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadAuthScheme, wchar_t>
    : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadAuthScheme& scheme, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMethod> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMethod& method, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMethod, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMethod& method, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadOperation> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadOperation& operation, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadOperation, wchar_t>
    : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadOperation& operation, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMode> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMode& mode, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::UploadMode, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::UploadMode& mode, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Encoding> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Encoding& encoding, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Encoding, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Encoding& encoding, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Kind> : public fmt::formatter<std::string_view>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Kind& kind, FormatContext& ctx) -> decltype(ctx.out());
};

template <>
struct fmt::formatter<Orc::OutputSpecTypes::Kind, wchar_t> : public fmt::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::OutputSpecTypes::Kind& kind, FormatContext& ctx) -> decltype(ctx.out());
};
