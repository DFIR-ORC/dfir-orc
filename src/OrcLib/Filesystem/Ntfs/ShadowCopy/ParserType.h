//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <optional>
#include <system_error>

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

enum class ParserType
{
    kUnknown,
    kMicrosoft,
    kInternal
};

ParserType ToParserType(std::wstring_view parser, std::error_code& ec);

std::string_view ToString(ParserType);
std::wstring_view ToWString(ParserType);

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
