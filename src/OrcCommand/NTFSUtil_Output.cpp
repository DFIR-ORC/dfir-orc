//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "NTFSUtil.h"
#include "SystemDetails.h"
#include "LogFileWriter.h"
#include "ToolVersion.h"

#include <string>

using namespace std;

using namespace Orc;
using namespace Orc::Command::NTFSUtil;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"	usage: DFIR-Orc.exe NTFSUtil (/usn|/record|/find|/loc|/enumlocs) <LocationPath> <Command Arguments>\r\n"
        L"\r\n"
        L"\t<LocationPath>        : Volume path to operate on (ex: \\\\.\\c:\\)\r\n"
        L"\r\n"
        L"\t/usn                  : Configure/View the USN journal configuration on current operating system\r\n"
        L"\t\tArguments: \r\n"
        L"\t\t/configure              : Configures USN journal for volume\r\n"
        L"\t\t/MaxSize=<MaxSize>      : Set USN journal maximum size to <MaxSize>\r\n"
        L"\t\t/SizeAtLeast=<Size>     : Set USN journal maximum size to at least <Size>\r\n"
        L"                              (only if current value is smaller than <Size>)\r\n"
        L"\t\t/Delta=<AllocDelta>     : Set USN journal allocation delta size to <AllocDelta>\r\n"
        L"\r\n"
        L"\r\n"
        L"\t/find <CmdArgs>       : Find a record (not yet implemented)\r\n"
        L"\r\n"
        L"\t/record=<RecordFRN>   : View details about a MFT record\r\n"
        L"\r\n"
        L"\t/HexDump <CmdArgs>    : Prints data from the disk\r\n"
        L"\t\tArguments: \r\n"
        L"\t\t/Offset=<offset>        : Specifies the offset to print\r\n"
        L"\t\t/Size=<length>          : Specifies the length (in bytes) to print\r\n\r\n"
        L"\t/MFT <CmdArgs>        : Prints Master file details\r\n"
        L"\t/loc                  : Prints out details about a location\r\n"
        L"\t/enumlocs             : Enumerates all possible locations\r\n"
        L"\r\n"
        L"\t/vss                  : List of existing volume shadow copies on current operating system\r\n"
        L"\t/bitlocker            : List BitLocker volumes metadata location and size\r\n"
        L"\t\tArguments: \r\n"
        L"\t\t/Offset=<offset>        : Specifies the offset of the -FVE-FS- start of BitLocker volume\r\n"
        L"\t\t<LocationPath>          : Path to BitLocker image (default is all mounted bitlocker volumes)\r\n"
        L"\r\n\tArgument: \r\n"
        L"\t/out=<Output>           : Output file or archive\r\n"
        L"\r\n");
    PrintCommonUsage();
    return;
}

void Main::PrintParameters()
{
    SaveAndPrintStartTime();

    PrintComputerName();

    if (!config.strVolume.empty())
    {
        log::Info(_L_, L"Volume name           : %s\r\n", config.strVolume.c_str());
    }

    return;
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
