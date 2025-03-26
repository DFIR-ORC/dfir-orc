//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ParserType.h"

#include <cctype>

namespace {

using namespace std::string_view_literals;

constexpr auto kUnknown = "unknown"sv;
constexpr auto kMicrosoft = "microsoft"sv;
constexpr auto kInternal = "internal"sv;

constexpr auto kUnknownW = L"unknown"sv;
constexpr auto kMicrosoftW = L"microsoft"sv;
constexpr auto kInternalW = L"internal"sv;

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

ParserType ToParserType(std::wstring_view parserType, std::error_code& ec)
{
    if (parserType.empty())
    {
        return ParserType::kUnknown;
    }

    std::wstring parser;
    std::transform(std::cbegin(parserType), std::cend(parserType), std::back_inserter(parser), [](wchar_t c) {
        return std::tolower(c);
    });

    constexpr std::array types = {
        std::pair(kUnknownW, ParserType ::kUnknown),
        std::pair(kMicrosoftW, ParserType::kMicrosoft),
        std::pair(kInternalW, ParserType::kInternal)};

    auto it = std::find_if(std::cbegin(types), std::cend(types), [parser](const auto& p) { return parser == p.first; });
    if (it == std::cend(types))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return static_cast<ParserType>(-1);
    }

    return it->second;
}

std::string_view ToString(ParserType parser)
{
    using namespace std::string_view_literals;

    switch (parser)
    {
        case ParserType::kMicrosoft:
            return kMicrosoft;
        case ParserType::kInternal:
            return kInternal;
        default:
            return "<unkown>"sv;
    }
}
std::wstring_view ToWString(ParserType parser)
{
    using namespace std::string_view_literals;

    switch (parser)
    {
        case ParserType::kMicrosoft:
            return kMicrosoftW;
        case ParserType::kInternal:
            return kInternalW;
        default:
            return L"<unkown>"sv;
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
