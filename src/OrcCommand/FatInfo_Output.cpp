//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//            fabienfl (ANSSI)
//
#include "stdafx.h"

#include "FatInfo.h"
#include "FatFileInfo.h"

#include "ToolVersion.h"

#include "Output/Text/Print/OutputSpec.h"
#include "Output/Text/Print/Intentions.h"
#include "Output/Text/Print/Filter.h"

#include "Usage.h"

using namespace Orc;
using namespace Orc::Command::FatInfo;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe FATInfo [/config=<ConfigFile>] [/Out=<Folder|Outfile.csv|Archive.7z>] "
        "[/(+|-)<ColumnSelection,...>:<Filter>] [/<DefaultColumnSelection>,...] [/KnownLocations|/kl] <Location> "
        "<Location2> ... <LocationN>",
        "FATInfo is intended to collect details on data stored on FAT mounted volumes, raw disk images. Basically, the "
        "tool enumerates the file system entries and outputs user-specified details to collect in one or more CSV "
        "file.");

    Usage::PrintOutputParameters(usageNode);

    {
        constexpr std::array kCustomLocationsParameters = {Usage::kKnownLocations};
        Usage::PrintLocationParameters(usageNode, kCustomLocationsParameters);
    }

    {
        constexpr std::array kCustomMiscParameters = {
            Usage::kMiscParameterComputer, Usage::kMiscParameterResurrectRecords, Usage::kMiscParameterCompression};

        Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);
    }

    Usage::PrintLoggingParameters(usageNode);

    Usage::PrintColumnSelectionParameter(usageNode, FatFileInfo::g_FatColumnNames, FatFileInfo::g_FatAliasNames);

    usageNode.AddEOL();
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValues(node, "Parsed locations", m_Config.locs.GetParsedLocations());

    PrintValue(node, "Output columns", m_Config.ColumnIntentions, FatFileInfo::g_FatColumnNames);
    PrintValue(node, "Default columns", m_Config.DefaultIntentions, FatFileInfo::g_FatColumnNames);
    PrintValue(node, "Filters", m_Config.Filters, FatFileInfo::g_FatColumnNames);

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
