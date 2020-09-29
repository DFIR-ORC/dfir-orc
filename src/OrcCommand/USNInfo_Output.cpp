//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "USNInfo.h"

#include <string>

#include "SystemDetails.h"

#include "ToolVersion.h"

#include "Output/Text/Print/OutputSpec.h"
#include "Output/Text/Print/Bool.h"
#include "Output/Text/Print/LocationSet.h"

#include "Usage.h"

using namespace Orc::Command::USNInfo;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe USNInfo [/config=<ConfigFile>] [/out=<Folder|Outfile.csv|Archive.7z>] "
        "<Location>...<LocationN>",
        "USNInfo collects information from the USN journal. It uses the same USN journal enumeration routines as "
        "NTFSInfo, but with FSCTL_READ_USN_JOURNAL. The USN journal is enumerated starting with the oldest entries and "
        "ending with the most recent.");

    Usage::PrintOutputParameters(usageNode);

    Usage::PrintLocationParameters(usageNode);

    constexpr std::array kSpecificParameters = {Usage::Parameter {
        "/Compact",
        "Non human readable output. When using this option, the full-path column is not filled in and the reason is in "
        "hexadecimal form in the output CSV file."}};

    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Output", config.output);

    PrintValues(node, "Parsed locations", config.locs.GetParsedLocations());
    PrintValue(node, "Compact", config.bCompactForm);

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
