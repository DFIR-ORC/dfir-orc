//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): jeanga (ANSSI)
//

#pragma once

#include <string>

#include <Utils/Result.h>

struct in_addr;
struct in6_addr;

namespace Orc {

Result<std::string> ToString(const in_addr& ip);
Result<std::wstring> ToWString(const in_addr& ip);

Result<std::string> ToString(const in6_addr& ip);
Result<std::wstring> ToWString(const in6_addr& ip);

}  // namespace Orc
