//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "NTFSUtil.h"

#include <array>
#include <string>

#include "SystemDetails.h"
#include "ToolVersion.h"
#include "Usage.h"
#include "Text/Print.h"
#include "Console.h"

using namespace std;

using namespace Orc::Command::NTFSUtil;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    using Parameter = Usage::Parameter;

    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe NTFSUtil [/config=<ConfigFile>] [/out=<Folder|Outfile.csv|Archive.7z>] <SUBCOMMAND>",
        "NTFS Swiss Army knife with a collection of useful features to investigate NTFS.");

    auto subcommandsNode = usageNode.AddNode("SUBCOMMAND");
    subcommandsNode.Add("Available commands: /usn, /vsn, /enumlocs, /loc, /record, /hexdump, /mft, /bitlocker");
    subcommandsNode.AddEOL();

    {
        auto usnNode = usageNode.AddNode("USN SUBCOMMAND");
        usnNode.Add("USN journal modification and expansion");
        usnNode.AddEOL();
        usnNode.Add("Usage: /usn <Location> [/configure [/maxSize=<Size>|/sizeAtLeast=<Size> /delta=<Size>]]");
        usnNode.AddEOL();

        constexpr std::array kUsnParameters = {
            Parameter {"<Location>", "Location (ex: '\\\\.\\harddiskvolume6'). See below for more details."},
            Parameter {"/configure", "Enable USN journal configuration"},
            Parameter {"/maxSize=<Size>", "Set the USN journal maximum size to <Size> bytes (requires /delta)"},
            Parameter {
                "/sizeAtLeast=<Size>",
                "Set the USN journal maximum size to be at least <Size> bytes (requires /delta)"},
            Parameter {"/delta", "Sets the USN journal allocation delta size to <Size> bytes"}};
        Usage::PrintParameters(usageNode, "USN PARAMETERS", kUsnParameters);
    }

    {
        auto vssNode = usageNode.AddNode("VSS SUBCOMMAND");
        vssNode.Add("Display informations about volume shadow copy");
        vssNode.AddEOL();
        vssNode.Add("Usage: /vss");
        vssNode.AddEOL();
    }

    {
        auto enumLocsNode = usageNode.AddNode("ENUMLOCS SUBCOMMAND");
        enumLocsNode.Add("Enumerate the available locations on a live system");
        enumLocsNode.AddEOL();
        enumLocsNode.Add("Usage: /enumlocs");
        enumLocsNode.AddEOL();
    }

    {
        auto locNode = usageNode.AddNode("LOC SUBCOMMAND");
        locNode.Add("Display informations about a location (physical drive, partitions, ...)");
        locNode.AddEOL();
        locNode.Add("Usage: /loc=<Location>");
        locNode.AddEOL();
    }

    {
        auto recordNode = usageNode.AddNode("RECORD SUBCOMMAND");
        recordNode.Add("Display informations about a MFT record");
        recordNode.AddEOL();
        recordNode.Add("Usage: /record=<FRN> <Location>");
        recordNode.AddEOL();

        constexpr std::array kRecordParameters = {
            Parameter {"<FRN>", "File Reference Number (FRN) of the record"},
            Parameter {"<Location>", "Location (ex: 'D:'). See below for more details."}};
        Usage::PrintParameters(usageNode, "RECORD PARAMETERS", kRecordParameters);
    }

    {
        auto hexdumpNode = usageNode.AddNode("HEXDUMP SUBCOMMAND");
        hexdumpNode.Add("Dump data from the disk. Record offset and size can be easily found with '/record' command");
        hexdumpNode.AddEOL();
        hexdumpNode.Add("Usage: /hexdump /offset=<Offset> /size=<Size> <Location>");
        hexdumpNode.AddEOL();

        constexpr std::array kHexdumpParameters = {
            Parameter {"/offset=<Offset>", "Dump begining offset"},
            Parameter {"/size=<Size>", "Size of the dump"},
            Parameter {"<Location>", "Location (ex: 'D:'). See below for more details."}};
        Usage::PrintParameters(usageNode, "HEXDUMP PARAMETERS", kHexdumpParameters);
    }

    {
        auto mftNode = usageNode.AddNode("MFT SUBCOMMAND");
        mftNode.Add("Display Master File Table (MFT) informations");
        mftNode.AddEOL();
        mftNode.Add("Usage: /mft <Location>");
        mftNode.AddEOL();

        constexpr std::array kMftParameters = {
            Parameter {"<Location>", "Location (ex: 'D:'). See below for more details."}};
        Usage::PrintParameters(usageNode, "MFT PARAMETERS", kMftParameters);
    }

    {
        auto bitlockerNode = usageNode.AddNode("BITLOCKER SUBCOMMAND");
        bitlockerNode.Add("List BitLocker volumes metadata location and size");
        bitlockerNode.AddEOL();
        bitlockerNode.Add("Usage: /bitlocker [/offset=<Offset>] [<Location>]");
        bitlockerNode.AddEOL();

        constexpr std::array kBitlockerParameters = {
            Parameter {"/offset=<Offset>", "Offset of the -FVE-FS- start of BitLocker volume"},
            Parameter {"<Location>", "Path to BitLocker image (default: all mounted bitlocker volumes)"}};
    }

    Usage::PrintLocationParameters(usageNode);
    Usage::PrintOutputParameters(usageNode);
    Usage::PrintLoggingParameters(usageNode);
    Usage::PrintMiscellaneousParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    if (!config.strVolume.empty())
    {
        PrintValue(node, L"Volume", config.strVolume);
    }

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
