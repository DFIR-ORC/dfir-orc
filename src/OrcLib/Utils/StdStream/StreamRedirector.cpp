//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "StreamRedirector.h"

namespace Orc {
namespace Command {

template class StreamRedirector<char>;
template class StreamRedirector<wchar_t>;

}  // namespace Command
}  // namespace Orc
