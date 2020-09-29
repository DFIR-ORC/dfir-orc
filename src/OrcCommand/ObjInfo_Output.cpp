//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ObjInfo.h"

#include "ToolVersion.h"

#include "Usage.h"

using namespace Orc::Command::ObjInfo;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe ObjInfo [/config=<ConfigFile>] [/out=<Folder|Outfile.csv|Archive.7z>]",
        "ObjInfo is a tool designed to list the set of Microsoft Windows named objects currently present on the "
        "analyzed system.");

    Usage::PrintOutputParameters(usageNode);
    Usage::PrintMiscellaneousParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

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
