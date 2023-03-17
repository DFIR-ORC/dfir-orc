//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2023 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include "Utils/Result.h"

namespace Orc {

enum class ResurrectRecordsMode : std::uint32_t
{
    kNo = 0,
    kYes,
    kResident,
    kCount
};

Result<std::string_view> ToString(ResurrectRecordsMode mode);
Result<std::wstring_view> ToWString(ResurrectRecordsMode mode);

Result<ResurrectRecordsMode> ToResurrectRecordsMode(std::string_view mode);
Result<ResurrectRecordsMode> ToResurrectRecordsMode(std::wstring_view mode);

}  // namespace Orc
