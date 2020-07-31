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

#include "ExtractData.h"
#include "FastFind.h"
#include "GetSamples.h"
#include "GetSectors.h"
#include "GetThis.h"
#include "ImportData.h"
#include "Mothership.h"
#include "NTFSInfo.h"
#include "RegInfo.h"
#include "NTFSUtil.h"
#include "ToolEmbed.h"
#include "USNInfo.h"
#include "WolfLauncher.h"
#include "ObjInfo.h"
#include "FatInfo.h"
#include "DD.h"
#include "GetComObjects.h"

#include "LogFileWriter.h"
#include "ToolVersion.h"

struct ToolDescription
{
    template <typename CommandType> static ToolDescription Get() {
        return ToolDescription{ CommandType::ToolName(), CommandType::ToolDescription(), UtilitiesMain::WMain<CommandType> };
    };
    LPCWSTR szName;
    LPCWSTR szDescr;
    std::function<int(int argc, const WCHAR* argv[])> WinMain;
};

using namespace Orc;
using namespace Orc::Command;

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
    ToolDescription::Get<ExtractData::Main>(),
    ToolDescription::Get<ImportData::Main>(),
    ToolDescription::Get<RegInfo::Main>(),
    ToolDescription::Get<DD::Main>(),
    ToolDescription::Get<GetComObjects::Main>(),
    {nullptr, nullptr}};

int Usage(const logger& pLog)
{
    log::Info(pLog, L"\r\nDFIR-Orc.Exe %s", kOrcFileVerStringW);

    const std::wstring metaName(kOrcMetaNameW);
    const std::wstring metaVersion(kOrcMetaVersionW);
    if (!metaName.empty() && !metaVersion.empty())
    {
        log::Info(pLog, L" (%s %s)\r\n", metaName.c_str(), metaVersion.c_str());
    }
    else
    {
        log::Info(pLog, L"\r\n");
    }

    log::Info(
        pLog,
        L"\r\n"
        L"	usage: DFIR-Orc.Exe <ToolName> <Tool Arguments>\r\n"
        L"\r\n"
        L"Available tools are:\r\n"
        L"\r\n");

    unsigned int index = 0;
    while (g_Tools[index].szName != nullptr)
    {
        log::Info(pLog, L"\t% 14s : %s\r\n", g_Tools[index].szName, g_Tools[index].szDescr);
        index++;
    }

    log::Info(pLog, L"\r\nMore information: DFIR-Orc.Exe <ToolName> /?\r\n");
    return -1;
}

int wmain(int argc, const WCHAR* argv[])
{
    concurrency::Scheduler::SetDefaultSchedulerPolicy(concurrency::SchedulerPolicy(1, concurrency::MaxConcurrency, 16));

    auto pLog = std::make_shared<LogFileWriter>();
    LogFileWriter::Initialize(pLog);

    if (argc > 1)
    {
        if (!_wcsicmp(argv[1], L"help"))
        {
            return Usage(pLog);
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
                else if (UtilitiesMain::IsProcessParent(L"explorer.exe", pLog))
                {
                    std::wcerr << "Press any key to continue..." << std::endl;
                    _getch();
                }
                else
                {
#ifdef _DEBUG
                    if (!UtilitiesMain::IsProcessParent(L"cmd.exe", pLog)
                        && !UtilitiesMain::IsProcessParent(L"WindowsTerminal.exe", pLog)
                        && !UtilitiesMain::IsProcessParent(L"pwsh.exe", pLog)
                        && !UtilitiesMain::IsProcessParent(L"VsDebugConsole.exe", pLog))
                    {
                        std::wcerr << "Press any key to continue..." << std::endl;
                        _getch();
                    }
#endif
                }

                return hr;
            }

            index++;
        }
    }

    if (EmbeddedResource::IsConfiguredToRun(pLog))
        return UtilitiesMain::WMain<Mothership::Main>(argc, argv);
    else
    {
        return Usage(pLog);
    }
}
