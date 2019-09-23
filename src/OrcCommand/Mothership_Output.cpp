//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Mothership.h"
#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace Orc;
using namespace Orc::Command::Mothership;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"usage: DFIR-Orc.Exe help for more information\r\n"
        L"\r\n");
}

void Main::PrintParameters()
{
    if (_L_->VerboseLog())
    {
        PrintBooleanOption(L"No wait", config.bNoWait);
        PrintBooleanOption(L"Preserve job", config.bPreserveJob);

        SaveAndPrintStartTime();
    }
}

void Main::PrintFooter()
{
    if (_L_->VerboseLog())
    {
        PrintExecutionTime();
    }
}
