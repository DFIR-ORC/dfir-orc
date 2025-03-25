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

#include <boost/algorithm/string/join.hpp>

#ifdef ORC_COMMAND_FASTFIND
#    include "Command/FastFind/FastFind.h"
#endif
#ifdef ORC_COMMAND_GETSAMPLES
#    include "Command/GetSamples/GetSamples.h"
#endif
#ifdef ORC_COMMAND_GETSECTORS
#    include "Command/GetSectors/GetSectors.h"
#endif
#ifdef ORC_COMMAND_GETTHIS
#    include "Command/GetThis/GetThis.h"
#endif
#ifdef ORC_COMMAND_NTFSINFO
#    include "Command/NTFSInfo/NTFSInfo.h"
#endif
#ifdef ORC_COMMAND_REGINFO
#    include "Command/RegInfo/RegInfo.h"
#endif
#ifdef ORC_COMMAND_NTFSUTIL
#    include "Command/NTFSUtil/NTFSUtil.h"
#endif
#ifdef ORC_COMMAND_TOOLEMBED
#    include "Command/ToolEmbed/ToolEmbed.h"
#endif
#ifdef ORC_COMMAND_USNINFO
#    include "Command/USNInfo/USNInfo.h"
#endif
#ifdef ORC_COMMAND_WOLFLAUNCHER
#    include "Command/WolfLauncher/WolfLauncher.h"
#endif
#ifdef ORC_COMMAND_OBJINFO
#    include "Command/ObjInfo/ObjInfo.h"
#endif
#ifdef ORC_COMMAND_FATINFO
#    include "Command/FatInfo/FatInfo.h"
#endif
#ifdef ORC_COMMAND_DD
#    include "Command/DD/DD.h"
#endif

#include "Mothership.h"
#include "Console.h"
#include "Text/Tree.h"
#include "ToolVersion.h"
#include "Usage.h"
#include "EmbeddedResource.h"
#include "Utils/WinApi.h"
#include "Utils/String.h"

using WinMainPtr = std::function<int(int argc, const WCHAR* argv[])>;

struct ToolDescription
{
    ToolDescription(LPCWSTR toolName, LPCWSTR toolDescr, WinMainPtr main)
        : szName(toolName)
        , szDescr(toolDescr)
        , WinMain(main)
    {
    }

    template <typename CommandType>
    static ToolDescription Get()
    {
        return ToolDescription(
            CommandType::ToolName(), CommandType::ToolDescription(), Orc::Command::UtilitiesMain::WMain<CommandType>);
    }

    LPCWSTR szName;
    LPCWSTR szDescr;
    WinMainPtr WinMain;
};

using namespace Orc::Command;
using namespace Orc::Text;
using namespace Orc;

ToolDescription g_Tools[] = {
#ifdef ORC_COMMAND_GETTHIS
    ToolDescription::Get<GetThis::Main>(),
#endif
#ifdef ORC_COMMAND_NTFSINFO
    ToolDescription::Get<NTFSInfo::Main>(),
#endif
#ifdef ORC_COMMAND_USNINFO
    ToolDescription::Get<USNInfo::Main>(),
#endif
#ifdef ORC_COMMAND_WOLFLAUNCHER
    ToolDescription::Get<Wolf::Main>(),
#endif
#ifdef ORC_COMMAND_FASTFIND
    ToolDescription::Get<FastFind::Main>(),
#endif
#ifdef ORC_COMMAND_OBJINFO
    ToolDescription::Get<ObjInfo::Main>(),
#endif
#ifdef ORC_COMMAND_GETSAMPLES
    ToolDescription::Get<GetSamples::Main>(),
#endif
#ifdef ORC_COMMAND_GETSECTORS
    ToolDescription::Get<GetSectors::Main>(),
#endif
#ifdef ORC_COMMAND_FATINFO
    ToolDescription::Get<FatInfo::Main>(),
#endif
#ifdef ORC_COMMAND_TOOLEMBED
    ToolDescription::Get<ToolEmbed::Main>(),
#endif
#ifdef ORC_COMMAND_NTFSUTIL
    ToolDescription::Get<NTFSUtil::Main>(),
#endif
#ifdef ORC_COMMAND_REGINFO
    ToolDescription::Get<RegInfo::Main>(),
#endif
#ifdef ORC_COMMAND_DD
    ToolDescription::Get<DD::Main>(),
#endif
    {nullptr, nullptr, nullptr}
};

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

void RelocateFile(const std::filesystem::path& source, const std::filesystem::path& destination, std::error_code& ec)
{
    auto exists = std::filesystem::exists(source, ec);
    if (ec)
    {
        Log::Debug(L"Failed to check existance for {}", source, ec);
        return;
    }

    if (exists == false)
    {
        return;
    }

    Log::Debug("Relocate {} to {}", source, destination);

    if (!MoveFileExW(destination.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
    {
        ec = LastWin32Error();
        Log::Debug("Failed MoveFileExW [{}]", ec);
        return;
    }

    if (!CopyFileW(source.c_str(), destination.c_str(), FALSE))
    {
        ec = LastWin32Error();
        Log::Debug("Failed CopyFileW [{}]", ec);
        return;
    }
}

void RelocateOnLocalDrive(std::error_code& ec)
{
    const std::filesystem::path mothership = GetModuleFileNameApi(NULL, ec);
    if (ec)
    {
        Log::Debug("Failed GetModuleFileNameApi [{}]", ec);
        return;
    }

    if (!PathIsNetworkPathW(mothership.c_str()))
    {
        return;
    }

    Log::Warn(
        "ORC is executing from a network drive, relocate to local drive to prevent connectivity issues during collect");

    const std::filesystem::path temp = GetTempPathApi(ec);
    if (ec)
    {
        Log::Debug("Failed GetTempPathApi [{}]", ec);
        return;
    }

    if (PathIsNetworkPathW(temp.c_str()))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Temporary directory is a network path");
        return;
    }

    std::filesystem::path localConfiguration = mothership;
    localConfiguration.replace_extension(L"xml");
    const std::filesystem::path newLocalConfiguration = temp / localConfiguration.filename();
    RelocateFile(localConfiguration, newLocalConfiguration, ec);
    if (ec)
    {
        return;
    }

    const std::filesystem::path newMothership = temp / mothership.filename();
    RelocateFile(mothership, newMothership, ec);
    if (ec)
    {
        return;
    }

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);

    std::vector<std::wstring> arguments;
    for (size_t i = 1; i < __argc; ++i)
    {
        arguments.emplace_back(__wargv[i]);
    }

    const auto commandLine = boost::join(arguments, " ");
    if (!CreateProcessW(
            newMothership.c_str(),
            const_cast<LPWSTR>(commandLine.c_str()),
            NULL,
            NULL,
            TRUE,
            CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            NULL,
            NULL,
            reinterpret_cast<STARTUPINFOW*>(&si),
            &pi))
    {
        ec = LastWin32Error();
        Log::Debug("Failed CreateProcessW [{}]", ec);
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ExitProcess(0);
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

#ifdef _DEBUG
                if (IsDebuggerPresent())
                {
                    DebugBreak();
                }
                else if (UtilitiesMain::IsProcessParent(L"explorer.exe"))
                {
                    std::wcerr << L"Press any key to continue..." << std::endl;
                    _getch();
                }
#else
                if (UtilitiesMain::IsProcessParent(L"explorer.exe"))
                {
                    std::wcerr << L"Press any key to continue..." << std::endl;
                    _getch();
                }
#endif

                return hr;
            }

            index++;
        }
    }

    if (EmbeddedResource::IsConfiguredToRun())
    {
        std::error_code ec;
        RelocateOnLocalDrive(ec);
        if (ec)
        {
            Log::Error("Failed to relocate on local drive [{}]", ec);
            ec.clear();
        }

        return UtilitiesMain::WMain<Mothership::Main>(argc, argv);
    }
    else
    {
        return PrintUsage();
    }
}
