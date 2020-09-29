//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include <string>
#include <array>

#include "NTFSInfo.h"

#include "ToolVersion.h"
#include "SystemDetails.h"
#include "Output/Text/Print/OutputSpec.h"
#include "Output/Text/Print/Intentions.h"
#include "Output/Text/Print/Filter.h"
#include "Output/Text/Print/LocationSet.h"

#include "Usage.h"

using namespace Orc::Command::NTFSInfo;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe NTFSInfo [/config=<ConfigFile>] [/out=<Folder|Outfile.csv|Archive.7z>] "
        "[/(+|-)<ColumnSelection,...>:<Filter>] [/<DefaultColumnSelection>,...] [/KnownLocations|/kl] "
        "<Location>...<LocationN>",
        "NTFSInfo is intended to collect details on data stored on NTFS mounted volumes, raw disk images or Volume "
        "Shadow Copies. Basically, the tool enumerates the file system entries and outputs user-specified details to "
        "collect in one or more CSV file.");

    constexpr std::array kCustomOutputParameters = {
        Usage::Parameter {"/FileInfo=<FilePath>", "File information file output specification"},
        Usage::Parameter {"/AttrInfo=<FilePath>", "Attribute information file output specification"},
        Usage::Parameter {"/I30Info=<FilePath>", "Timeline information file output specification"},
        Usage::Parameter {"/Timeline=<FilePath>", "File information file output specification"},
        Usage::Parameter {"/SecDecr=<FilePath>", "Security Descriptor information for the volume"}};

    Usage::PrintOutputParameters(usageNode, kCustomOutputParameters);

    constexpr std::array kCustomLocationsParameters = {Usage::kKnownLocations};
    Usage::PrintLocationParameters(usageNode, kCustomLocationsParameters);

    {
        constexpr std::array kCustomMiscParameters = {
            Usage::kMiscParameterComputer,
            Usage::kMiscParameterResurrectRecords,
            Usage::Parameter {"/SecDecr=<FilePath>", "Security Descriptor information for the volume"}};
        Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);
    }

    Usage::PrintLoggingParameters(usageNode);

    Usage::PrintColumnSelectionParameter(usageNode, NtfsFileInfo::g_NtfsColumnNames, NtfsFileInfo::g_NtfsAliasNames);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"FileInfo", config.outFileInfo);
    PrintValue(node, L"AttrInfo", config.outAttrInfo);
    PrintValue(node, L"I30Info", config.outI30Info);
    PrintValue(node, L"Timeline", config.outTimeLine);
    PrintValue(node, L"SecDescr", config.outSecDescrInfo);

    PrintValues(node, "Parsed locations", config.locs.GetParsedLocations());

    PrintValue(node, "Output columns", config.ColumnIntentions, NtfsFileInfo::g_NtfsColumnNames);
    PrintValue(node, "Default columns", config.DefaultIntentions, NtfsFileInfo::g_NtfsColumnNames);
    PrintValue(node, "Filters", config.Filters);

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintValue(node, "Lines processed", dwTotalFileTreated);
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
