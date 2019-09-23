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
        L"\r\n\
Usage: DFIR-Orc.exe GetSectors [options]\r\n\
\r\n\
Utility to dump disk sectors.\r\n\
Predefined logics are implemented to automatically dump data of interest like boot sectors or sectors outside any partition (disk slackspace).\r\n\
\r\n\
Dump options :\r\n\
  /LegacyBootCode       Predefined logic to dump MBR, VBRs and IPLs.\r\n\
  /UefiFull				Dump the entire EFI partition.\r\n\
  /UefiFullMaxSize		Maximum size to dump for the UEFI partition. A larger\r\n\
						partition will be truncated. Default : 400 Mo.\r\n\
  /SlackSpace           Predefined logic to dump sectors samples outside any\r\n\
                        partition.\r\n\
  /SlackSpaceDumpSize   Size in bytes of a slackspace sample. It defaults\r\n\
                        to 5 Mo.\r\n\
  /Custom               Dump a specific disk extent. Use /CustomOffset and\r\n\
                        /CustomSize.\r\n\
  /CustomOffset=OFFSET  Specify the specific disk extent offset in bytes.\r\n\
  /CustomSize=SIZE      Specify the specific disk extent size in bytes. It\r\n\
                        defaults to 512.\r\n\
\r\n\
General options :\r\n\
  /Out=OUTPUT           Specify the name of the container to write results.\r\n\
                        The container can be a folder or an archive (7z,\r\n\
                        zip, cab...).\r\n\
  /Disk=DEVICE          Specify the name of the disk device to read sectors\r\n\
                        from, in the form \"\\\\.\\PhysicalDriveX\". When\r\n\
                        not specified, it defaults to the Windows boot disk.\r\n\
  /NotLowInterface      Do not try to obtain a low interface on the disk device\r\n\
                        using the setupAPI functions.\r\n\
");
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
