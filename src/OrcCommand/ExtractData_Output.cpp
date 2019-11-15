//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

// TODO: accept certificate as input file
// TODO: only decrypt files ?
// TODO: remove report ?
// TODO: hunt long functions
// TODO: remove using std;
// TODO: add parameter password

#include "stdafx.h"

#include "ExtractData.h"
#include "ToolVersion.h"
#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command::ExtractData;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\""
        L"Usage: DFIR-Orc.exe ExtractData [/out=<Output>] [/report=report.csv] <directory|file.p7b>\r\n"
        L"\r\n"
        L"/config=<config.xml> : use xml configuration file\r\n"
        L"\t/out=<Output>      : output extraction directory\r\n"
        L"\t/report=<Output>      : extraction report output file\r\n"
        L"\t\r\n"
        L"<directory|file.p7b>   : input directory or input file to extract\r\n");

    PrintCommonUsage();
    return;
}

void Main::PrintParameters()
{
    log::Info(_L_, L"\r\n");

    SaveAndPrintStartTime();

    log::Info(_L_, L"\r\nExtractData configured to import:\r\n");
    for (auto& item : config.inputItems)
    {
        log::Info(_L_, L"\r\n\t%s\r\n\r\n", item.Description().c_str());
    }

    log::Info(_L_, L"\r\n");
    return;
}

void Main::PrintFooter()
{
    log::Info(_L_, L"\r\n");
    log::Info(_L_, L"Bytes processed     : %I64d\r\n", m_ullProcessedBytes);
    log::Info(_L_, L"\r\n");

    PrintExecutionTime();
}
