//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Mothership.h"

#include "ToolVersion.h"
#include "Usage.h"
#include "Text/Fmt/Boolean.h"

using namespace Orc::Command::Mothership;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(usageNode, "Usage: DFIR-Orc.exe help", "");
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);
    PrintValue(node, L"No wait", Traits::Boolean(config.bNoWait));
    PrintValue(node, L"Preserve job", Traits::Boolean(config.bPreserveJob));

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
