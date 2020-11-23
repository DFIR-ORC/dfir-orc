//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"
#include "SystemDetails.h"
#include "ToolVersion.h"
#include "Usage.h"
#include "Output/Text/Print/OutputSpec.h"
#include "Output/Text/Print/LocationSet.h"
#include "Output/Text/Print/Bool.h"

using namespace Orc::Command::Wolf;
using namespace Orc::Text;
using namespace Orc;

namespace Orc {
namespace Command {
namespace Wolf {

std::wstring Main::ToString(Main::WolfPowerState value)
{
    std::vector<std::wstring> properties;

    if (value == Main::WolfPowerState::Unmodified)
    {
        return L"<SystemDefault>";
    }

    if (value & Main::WolfPowerState::SystemRequired)
    {
        properties.push_back(L"SystemRequired");
    }

    if (value & Main::WolfPowerState::DisplayRequired)
    {
        properties.push_back(L"DisplayRequired");
    }

    if (value & Main::WolfPowerState::UserPresent)
    {
        properties.push_back(L"UserPresent");
    }

    if (value & Main::WolfPowerState::AwayMode)
    {
        properties.push_back(L"AwayMode");
    }

    return boost::join(properties, L",");
}

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe [/Config=<File>] [/Local=<File> [/Out=<Folder|File.csv|Archive.7z>] [/Outline=File.json] "
        "[/Once|/Overwrite|/CreateNew] [/Computer=<Name>] [/FullComputer=<Name>] [/PowerState=<Requirements>] "
        "[/ChildDebug|/NoChildDebug] [/Offline=<FilePath>] [/SystemType=<WorkStation|Server|DomainController>] [/keys]",
        "When DFIR-Orc executable is 'configured' it will be run in multiple processes which parent(s) will watch and "
        "administer (see online documentation). The top-level options specified here can affect parent and childs "
        "processes.");

    constexpr std::array kCustomOutputParameters = {
        Usage::Parameter {"/Outline=<File.json|File.xml>", "Generic system information file output"}};

    Usage::PrintOutputParameters(usageNode, kCustomOutputParameters);

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {
            "/CreateNew",
            "A new archive is created each time DFIR-Orc.exe is run, with names suffixed by _1.7z, _2.7z, etc. An "
            "alternative is to use the {TimeStamp} pattern in the archive name"},
        Usage::Parameter {
            "/Once", "If the target file already exists (and is not empty), then this archive is skipped"},
        Usage::Parameter {"/Overwrite", "Previous archives are overwritten by new executions of DFIR-Orc.exe"},
        Usage::Parameter {
            "/PowerState=<Requirements>",
            "Require system to stay on with one or more value from: SystemRequired, DisplayRequired, UserPresent, "
            "AwayMode (recommended: 'SystemRequired,AwayMode')"},
        Usage::Parameter {
            "/Offline=<FilePath>",
            "Sets the DFIR-Orc to work on a disk image. It will set %OfflineLocation% and explicitely select archive "
            "DFIR-ORC_Offline"},
        Usage::Parameter {
            "/Keys",
            "Display the archives and commands of a configured binary DFIR-Orc.exe. No command is executed, no "
            "archives are created."},
        Usage::Parameter {
            "/Key=<KeyWords>",
            "Comma separated list of commands to be executed or archive to be created (ex: 'GetYara,NTFSInfo) based on "
            "embedded configuration'"},
        Usage::Parameter {"/+Key=<KeyWord>", "Enables one or multiple archive generation or command execution"},
        Usage::Parameter {"/-Key=<KeyWord>", "Disable one or multiple archive generation or command execution"}};

    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    constexpr std::array kCustomMiscParameters = {
        Usage::kMiscParameterLocal,
        Usage::kMiscParameterComputer,
        Usage::kMiscParameterFullComputer,
        Usage::Parameter {
            "/SystemType=<WorkStation|Server|DomainController>",
            "Override the Windows edition type running on this computer"},
        Usage::kMiscParameterTempDir,
        Usage::kMiscParameterCompression,
        Usage::Parameter {"/ChildDebug", "Attach a debugger to child processes, dump memory in case of crash"},
        Usage::Parameter {"/NoChildDebug", "Block child debugging (if selected in config file)"},
        Usage::Parameter {
            "/archive_timeout",
            "Configures the time (in minutes) the engine will wait for the archive to complete. Upon timeout, this "
            "archive will be canceled"},
        Usage::Parameter {
            "/command_timeout",
            "Configures the time (in minutes) the engine will wait for the last command(s) to complete. Upon timeout, "
            "the command engine will stop, kill any pending process and move on with archive completion"},
        Usage::Parameter {
            "/WERDontShowUI",
            "Configures Windows Error Reporting to prevent blocking UI in case of a crash during DFIR ORC execution. "
            "WER previous configuration is restored at the end of DFIR ORC execution"}
        };

    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValues(node, L"Recipients", config.m_Recipients);
    PrintValue(node, L"Output", config.Output);
    PrintValue(node, L"TempDir", config.TempWorkingDir);
    PrintValue(node, L"Child debug", config.bChildDebug);
    PrintValue(node, L"CreateNew", config.bRepeatCreateNew);
    PrintValue(node, L"Once", config.bRepeatOnce);
    PrintValue(node, L"Overwrite", config.bRepeatOverwrite);
    PrintValue(node, L"Repeat behavior", WolfExecution::ToString(config.RepeatBehavior));
    PrintValue(node, L"Offline", config.strOfflineLocation.value_or(L"<empty>"));
    PrintValue(node, L"Log file", config.Log.Path);
    PrintValue(node, L"Outline file", config.Outline.Path);
    PrintValue(node, L"Priority", config.Priority);
    PrintValue(node, L"Power State", ToString(config.PowerState));
    PrintValue(node, L"Key selection", fmt::format(L"{}", boost::join(config.OnlyTheseKeywords, L", ")));
    PrintValues(node, L"Enable keys", config.EnableKeywords);
    PrintValues(node, L"Disable keys", config.DisableKeywords);

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

}  // namespace Wolf
}  // namespace Command
}  // namespace Orc
