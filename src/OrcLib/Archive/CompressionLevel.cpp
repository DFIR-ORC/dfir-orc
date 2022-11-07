//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Archive/CompressionLevel.h"

#include <map>
#include <cctype>

namespace {
using namespace std::string_view_literals;
constexpr auto kDefault = "default"sv;
constexpr auto kNone = "none"sv;
constexpr auto kFastest = "fastest"sv;
constexpr auto kFast = "fast"sv;
constexpr auto kNormal = "normal"sv;
constexpr auto kMaximum = "maximum"sv;
constexpr auto kUltra = "ultra"sv;

constexpr auto wkDefault = L"default"sv;
constexpr auto wkNone = L"none"sv;
constexpr auto wkFastest = L"fastest"sv;
constexpr auto wkFast = L"fast"sv;
constexpr auto wkNormal = L"normal"sv;
constexpr auto wkMaximum = L"maximum"sv;
constexpr auto wkUltra = L"ultra"sv;

}  // namespace

namespace Orc {
namespace Archive {

CompressionLevel ToCompressionLevel(std::wstring_view compressionLevel, std::error_code& ec)
{
    if (compressionLevel.empty())
    {
        return CompressionLevel::kDefault;
    }

    std::wstring level;
    std::transform(
        std::cbegin(compressionLevel), std::cend(compressionLevel), std::back_inserter(level), [](wchar_t c) {
            return std::tolower(c);
        });

    constexpr std::array levels = {
        std::pair(wkDefault, CompressionLevel::kDefault),
        std::pair(wkNone, CompressionLevel::kNone),
        std::pair(wkFastest, CompressionLevel::kFastest),
        std::pair(wkFast, CompressionLevel::kFast),
        std::pair(wkNormal, CompressionLevel::kNormal),
        std::pair(wkMaximum, CompressionLevel::kMaximum),
        std::pair(wkUltra, CompressionLevel::kUltra)};

    if (auto it = std::find_if(
            std::begin(levels), std::end(levels), [level](const auto& aLevel) { return level == aLevel.first; });
        it == std::cend(levels))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return CompressionLevel::kDefault;
    }
    else
        return it->second;
}

std::string_view ToString(CompressionLevel level)
{
    using namespace std::string_view_literals;
    switch (level)
    {
        case CompressionLevel::kDefault:
            return kDefault;
        case CompressionLevel::kNone:
            return kNone;
        case CompressionLevel::kFastest:
            return kFastest;
        case CompressionLevel::kFast:
            return kFast;
        case CompressionLevel::kNormal:
            return kNormal;
        case CompressionLevel::kMaximum:
            return kMaximum;
        case CompressionLevel::kUltra:
            return kUltra;
        default:
            return "<invalid>"sv;
    }
}
std::wstring_view ToWString(CompressionLevel level)
{
    using namespace std::string_view_literals;
    switch (level)
    {
        case CompressionLevel::kDefault:
            return wkDefault;
        case CompressionLevel::kNone:
            return wkNone;
        case CompressionLevel::kFastest:
            return wkFastest;
        case CompressionLevel::kFast:
            return wkFast;
        case CompressionLevel::kNormal:
            return wkNormal;
        case CompressionLevel::kMaximum:
            return wkMaximum;
        case CompressionLevel::kUltra:
            return wkUltra;
        default:
            return L"<invalid>"sv;
    }
}

}  // namespace Archive
}  // namespace Orc
