//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"

#include "LogFileWriter.h"
#include "SystemDetails.h"

#include "ToolVersion.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::Wolf;

void Main::PrintHeader(LPCWSTR szToolName, LPCWSTR szVersion)
{
    log::Info(_L_, L"\r\nDFIR-Orc Version %s\r\n", szVersion);
}

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"usage: DFIR-Orc.Exe [/config=<ConfigFile>] [/outdir=<Folder>] [/outfile=<OutputFile>] \r\n"
        L"\t/config=<ConfigFile>        : Specify a XML config file\r\n"
        L"\t/local=<ConfigFile>         : Specify a XML local config file\r\n"
        L"\r\n"
        L"\t/OutDir=<Folder>            : Output files will be created here\r\n"
        L"\t/utf8,/utf16		         : Select utf8 or utf16 enncoding (default is utf8, this is for CSV "
        L"files "
        L"only)\r\n"
        L"\r\n"
        L"\t/TempDir=<Folder>           : Temporary files will be created here\r\n"
        L"\r\n"
        L"\t/Keys                       : Displays the configured archives and commands keywords\r\n"
        L"\t/Key=<Keyword>,...          : Selects an explicit comma separated list of keywords to execute, all other "
        L"keywords are not executed/generated\r\n"
        L"\t/+Key=<Keyword>,...         : Enables optional archives or commands using a comma separated list of "
        L"keywords\r\n"
        L"\t/-Key=<Keyword>,...         : Disables archives or commands using a comma separated list of keywords\r\n"
        L"\r\n"
        L"\t/Once                       : Skip cab creation if file exists\r\n"
        L"\t/Overwrite                  : Overwrite existing file\r\n"
        L"\t/CreateNew                  : Create a unique new file name if file exists\r\n"
        L"\t/ChildDebug                 : Attach a debugger to child processes, dump memory in case of crash\r\n"
        L"\t/NoChildDebug               : Block child debugging (if selected in config file)\r\n"
        L"\t/Priority=<level>           : Change default process priority to Low, Normal or High\r\n"
        L"\t/PowerState=<Requirements>  : Require system to stay on. Possible values are SystemRequired, "
        L"DisplayRequired, UserPresent, AwayMode. Recommended: SystemRequired,AwayMode\r\n"
        L"\t/Computer=<ComputerName>    : Sets the OrcComputer name for all DFIR-Orc tools\r\n"
        L"\t/FullComputer=<ComputerName>: Sets the OrcFullComputer name for all DFIR-Orc tools\r\n"
        L"\t/SystemType=<SystemType>    : Sets the system type as typically used in {SystemType} to name archives"
        L"\t/Offline=<ImagePath>        : Sets the DFIR-Orc to work on a disk image, will set %OfflineLocation% and "
        L"explicitely select archive DFIR-ORC_Offline"
        L"\r\n"
        L"\t/verbose           : Turns on verbose logging\r\n"
        L"\t/debug             : Adds debug information (Source File Name, Line number) to output, outputs to debugger "
        L"(OutputDebugString)\r\n"
        L"\t/noconsole         : Turns off console logging\r\n"
        L"\t/logfile=<FileName>: All output is duplicated to logfile <FileName>\r\n"
        L"\r\n"
        L"\r\n");
}

void Main::PrintParameters()
{
    SaveAndPrintStartTime();
    PrintComputerName();
    PrintWhoAmI();
    PrintSystemType();
    PrintSystemTags();
    PrintOperatingSystem();

    if (config.strOfflineLocation.has_value())
    {
        log::Info(_L_, L"Offline location      : %s\r\n", config.strOfflineLocation.value().c_str());
    }

    PrintOutputOption(L"Output", config.Output);
    PrintOutputOption(L"Temp", config.TempWorkingDir);
    log::Info(_L_, L"Log file              : %s\r\n", config.strLogFileName.c_str());

    switch (config.RepeatBehavior)
    {
        case WolfExecution::CreateNew:
            log::Info(_L_, L"Repeat Behavior       : Create New file names\r\n");
            break;
        case WolfExecution::Once:
            log::Info(_L_, L"Repeat Behavior       : Run once then skip\r\n");
            break;
        case WolfExecution::Overwrite:
            log::Info(_L_, L"Repeat Behavior       : Overwrite existing output file names\r\n");
            break;
        case WolfExecution::NotSet:
            log::Info(_L_, L"Repeat Behavior       : No global override set (config behavior used)\r\n");
            break;
        default:
            log::Info(_L_, L"Repeat Behavior       : Invalid\r\n");
    }
    switch (config.Priority)
    {
        case Low:
            log::Info(_L_, L"Priority              : Low\r\n");
            break;
        case Normal:
            log::Info(_L_, L"Priority              : Normal\r\n");
            break;
        case High:
            log::Info(_L_, L"Priority              : High\r\n");
            break;
    }

    if (config.PowerState != Unmodified)
    {
        log::Info(_L_, L"Power management      : ");
        bool bFirst = true;
        if (config.PowerState & SystemRequired)
        {
            log::Info(_L_, L"%sSystemRequired", L"");
            bFirst = false;
        }
        if (config.PowerState & DisplayRequired)
        {
            log::Info(_L_, L"%sDisplayRequired", bFirst ? L"" : L",");
            bFirst = false;
        }
        if (config.PowerState & UserPresent)
        {
            log::Info(_L_, L"%sUserPresent", bFirst ? L"" : L",");
            bFirst = false;
        }
        if (config.PowerState & AwayMode)
        {
            log::Info(_L_, L"%sAwayMode", bFirst ? L"" : L",");
            bFirst = false;
        }
        log::Info(_L_, L"\r\n");
    }

    log::Info(_L_, L"\r\n");
    return;
}

void Main::PrintFooter()
{
    log::Info(_L_, L"\r\n");
    PrintExecutionTime();
    return;
}
