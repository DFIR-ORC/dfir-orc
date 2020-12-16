//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <functional>

#include "ToolEmbed.h"
#include "ToolVersion.h"
#include "Usage.h"

#include "Text/Print/OutputSpec.h"
#include "Text/Print/EmbedSpec.h"

SYSTEMTIME theStartTime;
SYSTEMTIME theFinishTime;
DWORD theStartTickCount;
DWORD theFinishTickCount;

using namespace std;

using namespace Orc::Command::ToolEmbed;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe ToolEmbed [/Config=<toolembed.xml> | /dump=<ConfiguredBinary.exe>] /out=dump-dir>",
        "ToolEmbed is used to add resources (binaries, configuration files) to a DFIR ORC binary. It takes an XML "
        "configuration file as input. ToolEmbed is also able to extract all the resources from a configured binary, "
        "thanks to the /dump option. This can be useful to quickly edit a configuration and obtain a new configured "
        "binary.");

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {
            "/Input=<Path>", "Path to the binary which ToolEmbed uses as a 'Mothership' or main executable"},
        Usage::Parameter {"/Output=<Path>", "Output file. Copy of the input file with the specified resources added"},
        Usage::Parameter {"/Run=<Resource>", "Specify which binary should be run by 'Mothership' or /Input binary"},
        Usage::Parameter {
            "/Run32=<Resource>",
            "Specify which binary should be run by 'Mothership' or '/Input' binary on 32 bit platform"},
        Usage::Parameter {
            "/Run64=<Resource>",
            "Specify which binary should be run by 'Mothership' or '/Input' binary on 64 bit platform"},
        Usage::Parameter {"/AddFile=<Path>,<Name>", "Add specified file <Path> as resource with name <Name>"},
        Usage::Parameter {"/Dump[=<Path>]", "Extract the resources from current or specified binary"}};

    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    constexpr std::array kCustomMiscParameters = {Usage::kMiscParameterComputer};
    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Input", config.strInputFile);
    PrintValue(node, L"Output", config.Output);

    std::vector<std::reference_wrapper<const EmbeddedResource::EmbedSpec>> toEmbed;
    std::vector<std::reference_wrapper<const EmbeddedResource::EmbedSpec>> toRemove;
    for (const auto& item : config.ToEmbed)
    {
        if (item.Type == EmbeddedResource::EmbedSpec::EmbedType::ValuesDeletion
            || item.Type == EmbeddedResource::EmbedSpec::EmbedType::BinaryDeletion)
        {
            toRemove.push_back({item});
            continue;
        }

        toEmbed.push_back(item);
    }

    PrintValues(node, L"Items to embed", toEmbed);
    PrintValues(node, L"Items to remove", toRemove);

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
