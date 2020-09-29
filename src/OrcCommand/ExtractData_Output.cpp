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

#include "Usage.h"

#include "Output/Text/Print.h"
#include "Utils/TypeTraits.h"

using namespace Orc;
using namespace Orc::Command::ExtractData;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe ExtractData [/Out=<Output>] [/Report=report.csv] <directory|file.p7b>",
        "Decrypt and decompress ORC archive files. Currently it is only supporting Microsoft Security Cryptography "
        "Service Provider and the keys have to be imported under user's certificate store (use 'certmgr.msc').");

    constexpr std::array kUsageInput = {
        Usage::Parameter {"<directory|file.p7b>", "input directory or input file to extract"}};
    Usage::PrintParameters(usageNode, L"INPUT PARAMETERS", kUsageInput);

    constexpr std::array kUsageOutput = {
        Usage::Parameter {"/Out=<Directory>", "Output file or directory"},
        Usage::Parameter {"/Report=<FilePath>", "Extraction information report"}};
    Usage::PrintParameters(usageNode, L"OUTPUT PARAMETERS", kUsageOutput);

    Usage::PrintMiscellaneousParameters(usageNode);

    Usage::PrintLoggingParameters(usageNode);

    constexpr std::array kUsageExamples = {Usage::Example {
        "DFIR-Orc.exe extractdata prod.p7b /Out=c:\\extract",
        "Extract and decrypt 'prod.p7b' in to 'c:\\extract\\' directory using MSCSP"}};
    Usage::PrintExamples(usageNode, kUsageExamples);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();

    for (auto& item : config.inputItems)
    {
        root.Add("{}", item.Description());
    }
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintValue(root, "Bytes processed", Traits::ByteQuantity(m_ullProcessedBytes));
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
