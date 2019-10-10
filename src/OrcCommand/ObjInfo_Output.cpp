//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ObjInfo.h"
#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace Orc;
using namespace Orc::Command::ObjInfo;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"	usage: DFIR-Orc.exe ObjInfo\r\n"
        L"\r\n"
        L"\t/out=<Output>       : Output file or archive\r\n");
    PrintCommonUsage();
    return;
}

void Main::PrintParameters()
{
    SaveAndPrintStartTime();

    PrintComputerName();

    PrintOperatingSystem();

    PrintOutputOption(config.output);

    return;
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
