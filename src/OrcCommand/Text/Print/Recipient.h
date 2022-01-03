//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Text/Print.h"

#include <boost/algorithm/string/join.hpp>

#include "WolfExecution.h"

namespace Orc {
namespace Text {

template <>
struct Printer<Orc::Command::Wolf::WolfExecution::Recipient>
{
    static void Output(Orc::Text::Tree& node, const Orc::Command::Wolf::WolfExecution::Recipient& recipient)
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
};

}  // namespace Text
}  // namespace Orc
