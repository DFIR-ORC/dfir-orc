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

typedef struct _ToolDescription
{
    LPCWSTR szName;
    LPCWSTR szDescr;
    std::function<int(int argc, const WCHAR* argv[])> WinMain;
} ToolDescription;

using namespace Orc;
using namespace Orc::Command;

ToolDescription g_Tools[] = {
    {ExtractData::Main::ToolName(), ExtractData::Main::ToolDescription(), UtilitiesMain::WMain<ExtractData::Main>},
    {FastFind::Main::ToolName(), FastFind::Main::ToolDescription(), UtilitiesMain::WMain<FastFind::Main>},
    {GetSamples::Main::ToolName(), GetSamples::Main::ToolDescription(), UtilitiesMain::WMain<GetSamples::Main>},
    {GetSectors::Main::ToolName(), GetSectors::Main::ToolDescription(), UtilitiesMain::WMain<GetSectors::Main>},
    {GetThis::Main::ToolName(), GetThis::Main::ToolDescription(), UtilitiesMain::WMain<GetThis::Main>},
    {ImportData::Main::ToolName(), ImportData::Main::ToolDescription(), UtilitiesMain::WMain<ImportData::Main>},
    {NTFSInfo::Main::ToolName(), NTFSInfo::Main::ToolDescription(), UtilitiesMain::WMain<NTFSInfo::Main>},
    {RegInfo::Main::ToolName(), RegInfo::Main::ToolDescription(), UtilitiesMain::WMain<RegInfo::Main>},
    {NTFSUtil::Main::ToolName(), NTFSUtil::Main::ToolDescription(), UtilitiesMain::WMain<NTFSUtil::Main>},
    {ToolEmbed::Main::ToolName(), ToolEmbed::Main::ToolDescription(), UtilitiesMain::WMain<ToolEmbed::Main>},
    {USNInfo::Main::ToolName(), USNInfo::Main::ToolDescription(), UtilitiesMain::WMain<USNInfo::Main>},
    {Wolf::Main::ToolName(), Wolf::Main::ToolDescription(), UtilitiesMain::WMain<Wolf::Main>},
    {ObjInfo::Main::ToolName(), ObjInfo::Main::ToolDescription(), UtilitiesMain::WMain<ObjInfo::Main>},
    {FatInfo::Main::ToolName(), FatInfo::Main::ToolDescription(), UtilitiesMain::WMain<FatInfo::Main>},
    {DD::Main::ToolName(), DD::Main::ToolDescription(), UtilitiesMain::WMain<DD::Main>},
    {GetComObjects::Main::ToolName(),
     GetComObjects::Main::ToolDescription(),
     UtilitiesMain::WMain<GetComObjects::Main>},
    {nullptr, nullptr}};

int Usage(const logger& pLog)
{

    log::Info(pLog, L"\r\nDFIR-Orc.Exe Version %s\r\n", WSTRFILEVER);
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
        log::Info(pLog, L"\t%s\r\n", g_Tools[index].szDescr);
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
                return g_Tools[index].WinMain(--argc, ++argv);
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
