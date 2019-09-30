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
#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::FastFind;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"usage: FastFind.Exe [/config=<ConfigFile>]...\r\n"
        L"\r\n"
        L"\t/verbose                    : Turns on verbose logging\r\n"
        L"\t/debug                      : Adds debug information (Source File Name, Line number) to output, outputs to "
        L"debugger (OutputDebugString)\r\n"
        L"\t/noconsole                  : Turns off console logging\r\n"
        L"\t/logfile=<FileName>         : All output is duplicated to logfile <FileName>\r\n"
        L"\t/filesystem=<FileName>      : All NTFS related finds are logged in <FileName>\r\n"
        L"\t/registry=<FileName>        : All registry related finds are logged in <FileName>\r\n"
        L"\t/object=<FileName>          : All System objects related finds are logged in <FileName>\r\n"
        L"\t/out=<FileName|Directory>   : All finds are logged into an XML file or directory\r\n"
        L"\t/yara=<Rules.Yara>          : Add rules files for Yara scan\r\n"
        L"\r\n");
    return;
}

void Main::PrintParameters()
{
    PrintComputerName();
    PrintOperatingSystem();

    if (!config.strVersion.empty())
    {
        log::Info(_L_, L"Version               : %s\r\n", config.strVersion.c_str());
    }
	PrintOutputOption(L"Filesystem", config.outFileSystem);
	PrintOutputOption(L"Registry", config.outRegsitry);
	PrintOutputOption(L"Object", config.outObject);
	PrintOutputOption(L"Structured", config.outStructured);

    SaveAndPrintStartTime();
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
