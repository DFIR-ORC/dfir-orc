//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jeremie Christin
//
#include "stdafx.h"
#include "GetSectors.h"
#include "ToolVersion.h"
#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command::GetSectors;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"Usage: DFIR-Orc.exe GetSectors [options]\r\n"
        L"\r\n"
        L"Utility to dump disk sectors.\r\n"
        L"Predefined logics are implemented to automatically dump data of interest like boot sectors or sectors outside any partition (disk slackspace).\r\n"
        L"\r\n"
        L"Dump options :\r\n"
        L"\t/LegacyBootCode       Predefined logic to dump MBR, VBRs and IPLs.\r\n"
        L"\t/UefiFull		      Dump the entire EFI partition.\r\n"
        L"\t/UefiFullMaxSize	  Maximum size to dump for the UEFI partition. A larger\r\n"
		L"\t                        partition will be truncated. Default : 400 Mo.\r\n"
        L"\t/SlackSpace           Predefined logic to dump sectors samples outside any\r\n"
        L"\t                        partition.\r\n"
        L"\t/SlackSpaceDumpSize   Size in bytes of a slackspace sample. It defaults\r\n"
        L"\t                        to 5 Mo.\r\n"
        L"\t/Custom               Dump a specific disk extent. Use /CustomOffset and\r\n"
        L"\t                        /CustomSize.\r\n"
        L"\t/CustomOffset=OFFSET  Specify the specific disk extent offset in bytes.\r\n"
        L"\t/CustomSize=SIZE      Specify the specific disk extent size in bytes. It\r\n"
        L"\t                        defaults to 512.\r\n"
        L"\r\n"
        L"\tGeneral options :\r\n"
        L"\t/out=OUTPUT           Specify the name of the container to write results.\r\n"
        L"\t                        The container can be a folder or an archive (7z,\r\n"
        L"\t                        zip, cab...).\r\n"
        L"\t/Disk=DEVICE          Specify the name of the disk device to read sectors\r\n"
        L"\t                        from, in the form \"\\\\.\\PhysicalDriveX\". When\r\n"
        L"\t                        not specified, it defaults to the Windows boot disk.\r\n"
        L"\t/NotLowInterface      Do not try to obtain a low interface on the disk device\r\n"
        L"\t                        using the setupAPI functions.\r\n"
       );

    PrintCommonUsage();

    return;
}

void Main::PrintParameters()
{
    SaveAndPrintStartTime();
    PrintComputerName();
    log::Info(_L_, L"Disk name             : %s\r\n", config.diskName.c_str());
    return;
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
