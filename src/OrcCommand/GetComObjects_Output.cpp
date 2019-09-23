//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetComObjects.h"
#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace Orc;
using namespace Orc::Command::GetComObjects;

void Main::PrintUsage()
{
    // Display the tool's usage
    log::Info(
        _L_,
        L"\r\n"
        L"	usage: DFIR-ORC.exe GetComObjects\r\n"
        L"\r\n"
        L"\t/out=<Output>       : Output file or archive\r\n"
        L"\t/config=<ConfigFile> : Specify a XML config file\r\n"
        L"\r\n"
        L"\t/low                : Runs with lowered priority\r\n"
        L"\t/verbose            : Turns on verbose logging\r\n"
        L"\t/debug              : Adds debug information (Source File Name, Line number) to output\r\n"
        L"\t/noconsole          : Turns off console logging\r\n"
        L"\t/logfile=<FileName> : All output is duplicated to logfile <FileName>\r\n");
    return;
}

void Main::PrintParameters()
{
    // Parameters are displayed when the configuration is complete and checked

    SaveAndPrintStartTime();

    PrintComputerName();

    PrintOperatingSystem();

    PrintOutputOption(m_Config.m_Output);

    return;
}

void Main::PrintFooter()
{
    // Footer is displayed when the Run exits
    PrintExecutionTime();
}
