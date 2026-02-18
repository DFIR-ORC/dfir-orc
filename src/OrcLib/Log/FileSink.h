//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include "FileSinkWin32.h"

namespace Orc {
namespace Log {

template <typename Mutex>
using FileSink = Orc::Log::FileSinkWin32<Mutex>;

}  // namespace Log
}  // namespace Orc
