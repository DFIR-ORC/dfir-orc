//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetSamples.h"
#include "ToolVersion.h"

#include <string>

#include "Usage.h"
#include "Text/Fmt/Boolean.h"
#include "Text/Print/OutputSpec.h"

using namespace Orc::Command::GetSamples;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe GetSamples [/Config=GetSamplesConfig.xml] [/GetThisConfig=GetThisConfig.xml] "
        "[/Autoruns[=<AutoRuns.xml>] [/out=<Folder|File.csv|Archive.7z>] ...",
        "GetSamples was developed to add automatic sample collection. DFIR ORC collects multiple artefacts, which in "
        "turn allow the analyst to pivot and determine which files to examine. GetSamples was created to identify and "
        "collect these files beforehand, to minimize the chances of having to get back to the analyzed system. "
        "Typically, targets include binaries registered in ASEP (AutoStart Extension Points), startup folders, loaded "
        "in processes, etc.");

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {"/Autoruns", "Always execute autorunsc.exe to collect ASE information"},
        Usage::Parameter {
            "/Autoruns=<autoruns.xml>", "Create autoruns.xml or use it as input to avoid running autoruns"},
        Usage::Parameter {"/GetThisArgs=<...>", "Arguments to be forwarded to 'GetThis' (ex: '/nolimits')"},
        Usage::Parameter {"/NoSigCheck", "Check only sample signatures from autoruns output"}};
    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    Usage::PrintLimitsParameters(usageNode);

    constexpr std::array kCustomOutputParameters = {
        Usage::Parameter {"/GetThisConfig=<FilePath>", "Output path to 'GetThis' generated configuration"},
        Usage::Parameter {"/SampleInfo=<FilePath.csv>", "Sample related information"},
        Usage::Parameter {"/TimeLine=<FilePath.csv>", "Timeline related information"}};
    Usage::PrintOutputParameters(usageNode, kCustomOutputParameters);

    constexpr std::array kCustomMiscParameters = {
        Usage::kMiscParameterCompression,
        Usage::kMiscParameterPassword,
        Usage::Parameter {"/TempDir=<DirectoryPath>", "Use 'DirectoryPath' to store temporary files"}};
    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode(L"Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Embedded autorun", Traits::Boolean(config.bRunAutoruns));

    if (config.autorunsOutput.Type != OutputSpec::Kind::None)
    {
        if (config.bKeepAutorunsXML)
        {
            PrintValue(node, L"Autoruns input XML", config.autorunsOutput.Path);
        }
        else
        {
            PrintValue(node, L"Autoruns output XML", config.autorunsOutput.Path);
        }
    }
    else
    {
        PrintValue(node, L"Autoruns XML", "<Not using ASE information>");
    }

    PrintValue(node, L"GetThis configuration", config.getThisConfig);
    PrintValue(node, L"Output", config.samplesOutput);
    PrintValue(node, L"Sample information", config.sampleinfoOutput);
    PrintValue(node, L"Timeline information", config.timelineOutput);
    PrintValue(node, L"Temporary output", config.tmpdirOutput);

    PrintValue(node, L"Ignore signatures", Traits::Boolean(config.bNoSigCheck));

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
