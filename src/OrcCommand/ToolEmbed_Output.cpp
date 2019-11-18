//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ToolEmbed.h"
#include "LogFileWriter.h"

#include "ToolVersion.h"

SYSTEMTIME theStartTime;
SYSTEMTIME theFinishTime;
DWORD theStartTickCount;
DWORD theFinishTickCount;

using namespace std;

using namespace Orc;
using namespace Orc::Command::ToolEmbed;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\n"
        L"usage: DFIR-Orc.exe ToolEmbed /Input=MotherShipCmd.exe [/Out=Tool.exe] [/AddFile=FileToEmbed.cab,101]+ "
        L"[/Name=Value]+\r\n"
        L"\r\n"
        L"\t/config=<ConfigFile>               : Specify a XML config file\r\n"
        L"\t/Input=MotherShipCmd.exe           : File used as input, if /output is not specified, input is modified\r\n"
        L"\t/Out=<Tool.exe>|<DumpDir>          : Make a copy of input to update with specified ressources\r\n"
        L"\t/AddFile=FileToEmbed.cab,CabName   : Embeds FileToEmbed.cab into Tool.exe as ressource CabName\r\n"
        L"\t/Run=cab:CabName|AutoTest.exe      : Extract&Run AutoTest.exe from embedded CabName ressource\r\n"
        L"\t/Run32=cab:CabName|AutoTest.exe    : Extract&Run AutoTest.exe from embedded CabName ressource only on x86 "
        L"platform\r\n"
        L"\t/Run64=cab:CabName|AutoTest_x64.exe: Extract&Run AutoTest.exe from embedded CabName ressource only on x64 "
        L"platform\r\n"
        L"\t/Name=Value                        : Adds ressource Name with content Value\r\n"
        L"\t/Dump=<configuredbinary.exe>       : Dumps the configured ressources into a folder (specified with "
        L"/out=<dir>) along with associated embed.xml\r\n"
        L"\r\n");
    PrintCommonUsage();
}

void Main::PrintParameters()
{

    SaveAndPrintStartTime();

    log::Info(_L_, L"\tInput  file   : %s\r\n", config.strInputFile.c_str());
    log::Info(_L_, L"\tOutput file   : %s\r\n", config.Output.Path.c_str());

    bool bFileAdditions = false;
    for (const auto& item : config.ToEmbed)
    {
        if (item.Type == EmbeddedResource::EmbedSpec::File)
        {
            if (!bFileAdditions)
            {
                log::Info(_L_, L"\r\n\tEmbed files: \r\n");
                bFileAdditions = true;
            }
            log::Info(_L_, L"\t\t%s with %s\r\n", item.Name.c_str(), item.Value.c_str());
        }
    }

    bool bNameValuePairs = false;
    for (const auto& item : config.ToEmbed)
    {
        if (item.Type == EmbeddedResource::EmbedSpec::NameValuePair)
        {
            if (!bNameValuePairs)
            {
                log::Info(_L_, L"\r\n\tName=Value pairs: \r\n");
                bNameValuePairs = true;
            }
            log::Info(_L_, L"\t\t%s = %s\r\n", item.Name.c_str(), item.Value.c_str());
        }
    }

    for (const auto& item : config.ToEmbed)
    {
        if (item.Type == EmbeddedResource::EmbedSpec::Archive)
        {
            log::Info(_L_, L"\r\n\tCreate archive: %s\r\n", item.Name.c_str());
            auto cabitems = item.ArchiveItems;
            for (const auto& anitem : cabitems)
            {
                log::Info(_L_, L"\t\t%s --> %s\r\n", anitem.Path.c_str(), anitem.Name.c_str());
            }
        }
    }

    bool bDeletions = false;
    for (const auto& item : config.ToEmbed)
    {
        if (item.Type == EmbeddedResource::EmbedSpec::ValuesDeletion
            || item.Type == EmbeddedResource::EmbedSpec::BinaryDeletion)
        {
            if (!bDeletions)
            {
                log::Info(_L_, L"\r\n\tRemove IDs: \t");
                bDeletions = true;
            }
            log::Info(_L_, L"%s ", item.Name.c_str());
        }
    }
    if (bDeletions)
        log::Info(_L_, L"\r\n");
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
