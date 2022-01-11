//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Recipient.h"

#include <string>
#include <vector>

#include <boost/algorithm/string/join.hpp>

namespace Orc {
namespace Text {

void Print(Tree& node, const Orc::Command::Wolf::WolfExecution::Recipient& recipient)
{
    std::vector<std::wstring> ArchiveSpec;
    auto archiveSpecs = boost::join(recipient.ArchiveSpec, L",");
    if (archiveSpecs.empty())
    {
        Print(node, recipient.Name);
    }
    else
    {
        Print(node, fmt::format(L"{} [{}]", recipient.Name, archiveSpecs));
    }
}

}  // namespace Text
}  // namespace Orc
