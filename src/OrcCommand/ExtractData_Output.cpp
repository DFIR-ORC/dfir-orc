//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "IMPORTDATA.h"
#include "ToolVersion.h"
#include "LogFileWriter.h"

#include <string>
#include <algorithm>

using namespace Orc;
using namespace Orc::Command::ImportData;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\""
        L"usage: DFIR-Orc.exe ImportData [/out=<Output>] <PathToImportedData.7z.p7b>*\r\n"
        L"\r\n"
        L"/config=<config.xml> : specifies a configuration file\r\n"
        L"\t/Out=<Output>      : output specification\r\n"
        L"\t\r\n"
        L"<PathToImportedData.7z.p7b>*    : Path to the data files to import\r\n");
    PrintCommonUsage();
    return;
}

void Main::PrintParameters()
{
    log::Info(_L_, L"\r\n");

    SaveAndPrintStartTime();

    log::Info(_L_, L"\r\nImportData configured to import:\r\n");
    for (auto& item : config.Inputs)
    {

        log::Info(_L_, L"\r\n\t%s\r\n\r\n", item.Description().c_str());
    }
    log::Info(_L_, L"\r\n");
    return;
}

void Main::PrintFooter()
{
    log::Info(_L_, L"\r\n");
    log::Info(_L_, L"Bytes processed     : %I64d\r\n", ullProcessedBytes);
    log::Info(_L_, L"Lines imported      : %I64d\r\n", ullImportedLines);
    log::Info(_L_, L"\r\n");
    PrintExecutionTime();
}
