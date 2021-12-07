//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <string>

#include "FastFind.h"

#include "SystemDetails.h"

#include "ToolVersion.h"

#include "Usage.h"

#include "Text/Print/OutputSpec.h"

using namespace Orc;
using namespace Orc::Command::FastFind;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe FastFind [/Config=<ConfigFile>] ...",
        "FastFind can leverage a collection of indicators to enable sophisticated indicator search. It can look up "
        "mounted file systems, registry or Windows objects directory using signature, patterns... Since FastFind aims "
        "to analyze thousands of systems, it requires minimal interaction. To achieve this goal, FastFind uses an XML "
        "configuration file embedded as a resource to specify the indicators to look for.");

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {
            "/Names=<Name>", "Add additional names to search terms (Kernel32.dll,nt*.sys,:ADSName,*.txt#EAName)"},
        Usage::Parameter {"/Version=<Description>", "Add a custom version description to the output"},
        Usage::Parameter {"/SkipDeleted", "Do not attempt to match against deleted records"},
        Usage::Parameter {"/Yara", "Add rules files for Yara scan"}};

    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    constexpr std::array kOutputParameters = {
        Usage::Parameter {"/Out=<FilePath>.xml", "Complete list of found items (xml)"},
        Usage::Parameter {"/FileSystem=<FilePath>.csv", "NTFS related found items"},
        Usage::Parameter {"/Object=<FilePath>.csv", "System objects related found items"}};

    Usage::PrintParameters(usageNode, Usage::kCategoryOutputParameters, kOutputParameters);

    Usage::PrintLocationParameters(usageNode);

    Usage::PrintLoggingParameters(usageNode);

    constexpr std::array kCustomMiscParameters = {Usage::kMiscParameterCompression};
    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    if (!config.strVersion.empty())
    {
        PrintValue(node, L"Version", config.strVersion);
    }

    PrintValue(node, L"Filesystem", config.outFileSystem);
    PrintValue(node, L"Registry", config.outRegsitry);
    PrintValue(node, L"Object", config.outObject);
    PrintValue(node, L"Structured", config.outStructured);
    PrintValue(node, L"Statistics", config.outStatistics);

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    auto root = m_console.OutputTree();

    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
