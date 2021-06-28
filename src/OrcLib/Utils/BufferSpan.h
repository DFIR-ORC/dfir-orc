//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <system_error>

#include <gsl/span>

namespace Orc {

template <typename T>
using BasicBufferSpan = gsl::span<T>;

using BufferSpan = BasicBufferSpan<uint8_t>;

}  // namespace Orc
