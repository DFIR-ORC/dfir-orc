//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "DD.h"

#include "ToolVersion.h"
#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command::DD;

void Main::PrintUsage()
{
    // Display the tool's usage
    log::Info(
        _L_,
        L"\r\n"
        L"	usage: DD.Exe \r\n"
        L"\r\n"
        L"\t/out=<Output>       : Output file or archive\r\n"
        L"\t/if=<input_stream>  : Input stream\r\n"
        L"\t/of=<output_stream> : Output streams\r\n"
        L"\t/bs=<BlockSize>     : Copy per blocks of <BlockSize> bytes (default is 512)\r\n"
        L"\t/count=<BlockCount> : Copy <BlockCount> blocks of <BlockSize> bytes\r\n"
        L"\t/skip=<BlockCount>  : Skip <BlockCount> blocks of <BlockSize> bytes from input device\r\n"
        L"\t/seek=<BlockCount>  : Seek <BlockCount> blocks of <BlockSize> bytes on ouput device\r\n"
        L"\t/hash=<hashes>      : Comma separatelist of supported hash function (MD5|SHA1|SHA256)\r\n"
        L"\t/no_error           : Continue on error\r\n"
        L"\t/notrunc            : Do not truncate output stream\r\n"
        );
    PrintCommonUsage();

    return;
}

void Main::PrintParameters()
{
    // Parameters are displayed when the configuration is complete and checked

    SaveAndPrintStartTime();

    PrintComputerName();

    PrintOperatingSystem();

    PrintOutputOption(config.output);

    PrintStringOption(L"Input", config.strIF.c_str());

    for (const auto& out : config.OF)
    {
        PrintStringOption(L"Output", out.c_str());
    }

    if (config.Count.QuadPart > 0LL)
        PrintIntegerOption(L"Count (blocks)", config.Count.QuadPart);
    if (config.Skip.QuadPart > 0LL)
        PrintIntegerOption(L"Skip (input)", config.Skip.QuadPart);
    if (config.Seek.QuadPart > 0LL)
        PrintIntegerOption(L"Seek (output)", config.Seek.QuadPart);

    PrintBooleanOption(L"No Error", config.NoError);

    log::Info(_L_, L"\r\n\r\n");

    return;
}

void Main::PrintFooter()
{
    // Footer is displayed when the Run exits
    PrintExecutionTime();
}
