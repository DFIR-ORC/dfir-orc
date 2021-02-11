//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "UncheckedStreambuf.h"

namespace Orc {
namespace Command {

template class UncheckedStreambuf<char>;
template class UncheckedStreambuf<wchar_t>;

}  // namespace Command
}  // namespace Orc
