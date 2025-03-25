//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jeremie Christin
//            fabienfl (ANSSI)
//
#include "stdafx.h"
#include "GetSectors.h"
#include "ToolVersion.h"

#include "Usage.h"

#include "Utils/TypeTraits.h"
#include "Text/Fmt/Limit.h"
#include "Text/Fmt/BoolOnOff.h"

using namespace Orc::Command::GetSectors;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe GetSectors [/Config=<ConfigFile>] [/Out=<Folder|Outfile.csv|Archive.7z>] "
        "[/LegacyBootCode] [/UefiFull] [/UefiFullMaxSize] [/SlackSpace] [/SlackSpaceDumpSize] [/Custom] "
        "[/CustomOffset=<offset>] [/CustomSize=<size>] [/Disk=<device>] [/NotLowInterface]",
        "GetSectors is designed to collect low-level disk data, i.e. data not related to the file system. As such, it "
        "can typically be used to collect the boot sector, the boot code, the partition tables, slack space on the "
        "disk (typically the available sectors after the last partition), etc.");

    Usage::PrintOutputParameters(usageNode);

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {"/LegacyBootCode", "Dump MBR, VBRs and IPLs"},
        Usage::Parameter {"/UefiFull", "Dump the entire EFI partition"},
        Usage::Parameter {"/UefiFullMaxSize", "Maximum dump size for the UEFI partition (default: 5MB)"},
        Usage::Parameter {"/SlackSpace", "Dump sectors samples outside any partition"},
        Usage::Parameter {"/SlackSpaceDumpSize", "Byte size of a slackspace sample (default: 5MB)"},
        Usage::Parameter {"/Custom", "Enable custom dump mode and requires /CustomOffset and /CustomSize"},
        Usage::Parameter {"/CustomOffset=<offset>", "Disk extent offset"},
        Usage::Parameter {"/CustomSize=<size>", "Disk extent byte size in bytes (default: 512)"},
        Usage::Parameter {
            "/Disk=<device>",
            "Specifies the name of the disk device to read sectors from (ex: '\\\\.\\PhysicalDrive0', 'd:\\disk.dd')"},
        Usage::Parameter {
            "/NotLowInterface",
            "The tool does not try to obtain a low interface on the disk device using the setupAPI functions"}};

    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    constexpr std::array kCustomMiscParameters = {Usage::kMiscParameterCompression};
    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Disk name", config.diskName);
    PrintValue(node, L"LegacyBootCode", Traits::BoolOnOff(config.dumpLegacyBootCode));
    PrintValue(node, L"UefiFull", Traits::BoolOnOff(config.dumpUefiFull));
    PrintValue(node, L"UefiFullMaxSize", Traits::ByteQuantity(config.uefiFullMaxSize));
    PrintValue(node, L"SlackSpace", Traits::BoolOnOff(config.dumpSlackSpace));
    PrintValue(node, L"SlackSpaceSize", Traits::ByteQuantity(config.slackSpaceDumpSize));
    PrintValue(node, L"Custom", Traits::BoolOnOff(config.customSample));
    PrintValue(node, L"CustomOffset", config.customSampleOffset);
    PrintValue(node, L"CustomSize", Traits::ByteQuantity(config.customSampleSize));
    PrintValue(node, L"NotLowInterface", Traits::BoolOnOff(!config.lowInterface));

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    auto root = m_console.OutputTree();

    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
