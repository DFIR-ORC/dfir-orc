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

namespace Orc {
namespace Archive {

CompressionLevel ToCompressionLevel(const std::wstring& compressionLevel, std::error_code& ec)
{
    std::wstring level;
    std::transform(
        std::cbegin(compressionLevel), std::cend(compressionLevel), std::back_inserter(level), [](wchar_t c) {
            return std::tolower(c);
        });

    const std::map<std::wstring, CompressionLevel> map = {
        {L"default", CompressionLevel::kDefault},
        {L"none", CompressionLevel::kNone},
        {L"fastest", CompressionLevel::kFastest},
        {L"fast", CompressionLevel::kFast},
        {L"normal", CompressionLevel::kNormal},
        {L"maximum", CompressionLevel::kMaximum},
        {L"ultra", CompressionLevel::kUltra}};

    auto it = map.find(level);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return CompressionLevel::kDefault;
    }

    return it->second;
}

}  // namespace Archive
}  // namespace Orc
