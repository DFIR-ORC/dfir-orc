//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>

[[nodiscard]] std::wstring FormatHumanSize(uint64_t bytes);
[[nodiscard]] std::wstring FormatRawBytes(uint64_t bytes);
