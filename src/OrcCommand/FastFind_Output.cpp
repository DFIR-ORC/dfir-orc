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
		L"\t/config=<ConfigFile>        : Configuration file (or resource reference)\r\n"
        L"\t/filesystem=<FileName>.csv  : All NTFS related finds are logged in <FileName>.csv\r\n"
        L"\t/registry=<FileName>.csv    : All registry related finds are logged in <FileName>.csv\r\n"
        L"\t/object=<FileName>.csv      : All System objects related finds are logged in <FileName>.csv\r\n"
        L"\t/out=<FileName.xml>         : All finds are logged into an XML file\r\n"
        L"\t/yara=<Rules.Yara>          : Add rules files for Yara scan\r\n"
		L"\t/SkipDeleted                : Do not attempt to match against deleted records\r\n"
		L"\t/Names=<NamesSpec>          : Add names to search terms (Kernel32.dll,nt*.sys,:ADSName,*.txt#EAName)\r\n"
		L"\t/Version=<Description>      : Add a version description to FastFind output\r\n"
        L"\r\n");
    PrintCommonUsage();
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
