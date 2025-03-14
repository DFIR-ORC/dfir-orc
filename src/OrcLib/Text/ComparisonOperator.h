//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/Result.h"
#include "Utils/ComparisonOperator.h"

namespace Orc {
namespace Text {

Result<std::pair<ComparisonOperator, std::string_view>>
ComparisonOperatorFromString(std::string_view expression, std::optional<ComparisonOperator> defaultOperator = {});

Result<std::pair<ComparisonOperator, std::wstring_view>>
ComparisonOperatorFromString(std::wstring_view expression, std::optional<ComparisonOperator> defaultOperator = {});

std::string_view ToStringView(ComparisonOperator op, bool displayEqual = true);
std::wstring_view ToWStringView(ComparisonOperator op, bool displayEqual = true);

}  // namespace Text
}  // namespace Orc
