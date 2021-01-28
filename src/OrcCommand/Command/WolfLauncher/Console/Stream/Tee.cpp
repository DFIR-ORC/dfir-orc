//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "Tee.h"

namespace Orc {
namespace Command {

template class Tee<char>;
template class Tee<wchar_t>;

}  // namespace Command
}  // namespace Orc
