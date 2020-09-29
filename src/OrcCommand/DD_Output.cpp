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

#include "Usage.h"

#include "Output/Text/Fmt/formatter.h"
#include "Output/Text/Print/Bool.h"
#include "Output/Text/Print/OutputSpec.h"

using namespace Orc;
using namespace Orc::Command::DD;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe DD [/out=<Folder|Outfile.csv|Archive.7z>] /if=<InputLocation> /of=<OutputLocation> "
        "/bs=<BlockSize> /count=<BlockCount> [/skip=<BlockCount>] [/seek=<BlockCount>] [/hash=<Hashes>] [/noerror] "
        "[/notrunc]",
        "Dump tool inspired from linux 'dd' command");

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {"/IF=<InputLocation>", "Source stream, see Location section"},
        Usage::Parameter {"/OF=<OutputLocation>", "Output stream, see Location section"},
        Usage::Parameter {"/BS=<BlockSize>", ""},
        Usage::Parameter {"/Count=<BlockCount>", ""},
        Usage::Parameter {"/Skip=<BlockCount>", ""},
        Usage::Parameter {"/Seek=<BlockCount>", ""},
        Usage::Parameter {"/Hash=<Hashes>", ""},
        Usage::Parameter {"/NoError", ""},
        Usage::Parameter {"/NoTrunc", ""},
    };

    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    Usage::PrintLocationParameters(usageNode);

    Usage::PrintOutputParameters(usageNode);

    Usage::PrintMiscellaneousParameters(usageNode);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Output report", config.output);
    PrintValue(node, L"Input", config.strIF);
    PrintValues(node, L"Output(s)", config.OF);

    if (config.BlockSize.QuadPart > 0LL)
    {
        PrintValue(node, L"Block size", Traits::ByteQuantity(config.BlockSize.QuadPart));
    }

    if (config.Count.QuadPart > 0LL)
    {
        PrintValue(node, L"Block count", config.Count.QuadPart);
    }

    if (config.Skip.QuadPart > 0LL)
    {
        PrintValue(node, L"Skipped block", config.Skip.QuadPart);
    }

    if (config.Seek.QuadPart > 0LL)
    {
        PrintValue(node, L"Seek", config.Seek.QuadPart);
    }

    PrintValue(node, L"No Error", config.NoError);
    PrintValue(node, L"No Truncation", config.NoTrunc);
    PrintValue(node, L"Hashs", config.Hash);
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
