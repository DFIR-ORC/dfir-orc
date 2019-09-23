//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "USNInfo.h"

#include <string>

#include "SystemDetails.h"
#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::USNInfo;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"usage: DFIR-Orc.exe USNInfo [/out=<Folder>|<OutputFile>|<OutputArchive>] <Dir1> <Dir2> ... <DirN>\r\n"
        L"\r\n"
        L"\t/out=<OutputSpec>        : output specification\r\n"
        L"\t\tOutput specification can be one of:\r\n"
        L"\t\t\tA file that will contain output for all locations\r\n"
        L"\t\t\tA directory that will contain one file per location (<Output>_<Location identifier>.csv)\r\n"
        L"\t\t\tA SQL connection string and table name to import into (<connectionstring>#<tablename>)\r\n"
        L"\r\n"
        L"\t/Compact                 : Compact form (no paths, only reason flag\r\n"
        L"\t/utf8,/utf16			  : Select utf8 or utf16 enncoding (default is utf8)\r\n"
        L"\r\n");
}

void Main::PrintParameters()
{
    SaveAndPrintStartTime();
    PrintComputerName();
    PrintOperatingSystem();

    PrintOutputOption(config.output);

    log::Info(_L_, L"Log format            : %s\r\n", config.bCompactForm ? L"Compact" : L"Full");

    config.locs.PrintLocations(true);
    return;
}

void Main::PrintFooter()
{
    PrintExecutionTime();
    return;
}
