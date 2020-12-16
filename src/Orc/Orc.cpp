//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// OrcUtil::Main.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "FastFind.h"
#include "GetSamples.h"
#include "GetSectors.h"
#include "GetThis.h"
#include "Mothership.h"
#include "NTFSInfo.h"
#include "RegInfo.h"
#include "NTFSUtil.h"
#include "ToolEmbed.h"
#include "USNInfo.h"
#include "Command/WolfLauncher/WolfLauncher.h"
#include "ObjInfo.h"
#include "FatInfo.h"
#include "DD.h"

#include "Output/Console/Console.h"
#include "Output/Text/Tree.h"

#include "ToolVersion.h"
#include "Usage.h"

struct ToolDescription
{
    template <typename CommandType>
    static ToolDescription Get()
    {
        return ToolDescription {
            CommandType::ToolName(), CommandType::ToolDescription(), UtilitiesMain::WMain<CommandType>};
    };
    LPCWSTR szName;
    LPCWSTR szDescr;
    std::function<int(int argc, const WCHAR* argv[])> WinMain;
};

using namespace Orc::Command;
using namespace Orc::Output;
using namespace Orc;

ToolDescription g_Tools[] = {
    ToolDescription::Get<GetThis::Main>(),
    ToolDescription::Get<NTFSInfo::Main>(),
    ToolDescription::Get<USNInfo::Main>(),
    ToolDescription::Get<Wolf::Main>(),
    ToolDescription::Get<FastFind::Main>(),
    ToolDescription::Get<ObjInfo::Main>(),
    ToolDescription::Get<GetSamples::Main>(),
    ToolDescription::Get<GetSectors::Main>(),
    ToolDescription::Get<FatInfo::Main>(),
    ToolDescription::Get<ToolEmbed::Main>(),
    ToolDescription::Get<NTFSUtil::Main>(),
    ToolDescription::Get<RegInfo::Main>(),
    ToolDescription::Get<DD::Main>(),
    {nullptr, nullptr}};

int PrintUsage()
{
    const std::wstring metaName(kOrcMetaNameW);
    const std::wstring metaVersion(kOrcMetaVersionW);

    Console console;

    if (!metaName.empty() && !metaVersion.empty())
    {
        console.Print(L"DFIR-Orc.exe {} ({} {})", kOrcFileVerStringW, metaName, metaVersion);
    }
    else
    {
        console.Print("DFIR-Orc.exe {}", kOrcFileVerString);
    }

    console.PrintNewLine();

    console.Print("Usage: DFIR-Orc.exe <ToolName> <Tool Arguments>");

    console.PrintNewLine();

    auto root = console.CreateOutputTree(0, 1, L"Available tools are:");
    console.PrintNewLine();

    for (size_t index = 0; g_Tools[index].szName != nullptr; ++index)
    {
        Orc::Text::PrintValue(root, g_Tools[index].szName, g_Tools[index].szDescr);
    }

    console.PrintNewLine();
    console.Print(L"Display help on a specific tool with: 'DFIR-Orc.exe <ToolName> /?'");
    console.PrintNewLine();

    return -1;
}

int wmain(int argc, const WCHAR* argv[])
{
    concurrency::Scheduler::SetDefaultSchedulerPolicy(concurrency::SchedulerPolicy(1, concurrency::MaxConcurrency, 16));

    if (argc > 1)
    {
        if (!_wcsicmp(argv[1], L"help"))
        {
            return PrintUsage();
        }

        unsigned int index = 0;
        while (g_Tools[index].szName != nullptr)
        {
            if (!_wcsicmp(argv[1], g_Tools[index].szName))
            {
                HRESULT hr = g_Tools[index].WinMain(--argc, ++argv);

                // if parent is 'explorer' or debugger attached press any key to continue
                if (IsDebuggerPresent())
                {
                    DebugBreak();
                }
                else if (UtilitiesMain::IsProcessParent(L"explorer.exe"))
                {
                    std::wcerr << L"Press any key to continue..." << std::endl;
                    _getch();
                }

                return hr;
            }

            index++;
        }
    }

    if (EmbeddedResource::IsConfiguredToRun())
    {
        return UtilitiesMain::WMain<Mothership::Main>(argc, argv);
    }
    else
    {
        return PrintUsage();
    }
}
