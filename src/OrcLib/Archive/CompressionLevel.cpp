//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Archive/CompressionLevel.h"

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

constexpr auto kDefaultW = L"default"sv;
constexpr auto kNoneW = L"none"sv;
constexpr auto kFastestW = L"fastest"sv;
constexpr auto kFastW = L"fast"sv;
constexpr auto kNormalW = L"normal"sv;
constexpr auto kMaximumW = L"maximum"sv;
constexpr auto kUltraW = L"ultra"sv;

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
        std::pair(kDefaultW, CompressionLevel::kDefault),
        std::pair(kNoneW, CompressionLevel::kNone),
        std::pair(kFastestW, CompressionLevel::kFastest),
        std::pair(kFastW, CompressionLevel::kFast),
        std::pair(kNormalW, CompressionLevel::kNormal),
        std::pair(kMaximumW, CompressionLevel::kMaximum),
        std::pair(kUltraW, CompressionLevel::kUltra)};

    auto it = std::find_if(std::cbegin(levels), std::cend(levels), [level](const auto& l) { return level == l.first; });
    if (it == std::cend(levels))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return CompressionLevel::kDefault;
    }

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
            return kDefaultW;
        case CompressionLevel::kNone:
            return kNoneW;
        case CompressionLevel::kFastest:
            return kFastestW;
        case CompressionLevel::kFast:
            return kFastW;
        case CompressionLevel::kNormal:
            return kNormalW;
        case CompressionLevel::kMaximum:
            return kMaximumW;
        case CompressionLevel::kUltra:
            return kUltraW;
        default:
            return L"<invalid>"sv;
    }
}

}  // namespace Archive
}  // namespace Orc
