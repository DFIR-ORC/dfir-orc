//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include <array>
#include <optional>

#include <windows.h>

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Cmd/CmdMain/CmdAdd.h"
#include "Cmd/CmdMain/CmdGet.h"
#include "Cmd/CmdMain/CmdRemove.h"
#include "Cmd/CmdMain/CmdInfo.h"
#include "Cmd/CmdMain/CmdRun.h"
#include "Cmd/CmdResource.h"
#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"
#include "OrcCapsuleCli.h"

namespace fs = std::filesystem;

namespace {

void SetupDllLookupDirectories()
{
    {
        using FnSetDllDirectoryW = BOOL(__stdcall*)(LPCWSTR lpPathName);

        FnSetDllDirectoryW fnSetDllDirectory =
            reinterpret_cast<FnSetDllDirectoryW>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetDllDirectoryW"));
        if (fnSetDllDirectory == nullptr)
        {
            Log::Debug("Missing SetDllDirectoryW");
        }
        else if (fnSetDllDirectory(L"") == FALSE)
        {
            Log::Critical("Failed SetDllDirectoryW [{}]", LastWin32Error());
        }
    }

    {
        const auto kLOAD_LIBRARY_SEARCH_SYSTEM32 = 0x00000800;
        const auto kLOAD_LIBRARY_SEARCH_USER_DIRS = 0x00000400;

        using FnSetDefaultDllDirectories = BOOL(__stdcall*)(DWORD DirectoryFlags);

        FnSetDefaultDllDirectories fnSetDefaultDllDirectories = reinterpret_cast<FnSetDefaultDllDirectories>(
            GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetDefaultDllDirectories"));
        if (fnSetDefaultDllDirectories == nullptr)
        {
            Log::Debug("Missing SetDefaultDllDirectories");
        }
        else if (fnSetDefaultDllDirectories(kLOAD_LIBRARY_SEARCH_SYSTEM32) == FALSE)
        {
            Log::Critical("Failed SetDefaultDllDirectories [{}]", LastWin32Error());
        }
    }
}

}  // namespace

int wmain(int argc, const wchar_t* argv[])
{
    std::error_code ec;

    SetupDllLookupDirectories();

    CapsuleOptions opts = ParseCommandLine(argc, argv);

#ifdef _DEBUG
    Log::SetLogLevel(opts.logLevel.value_or(Log::Level::Debug));
#else
    Log::SetLogLevel(opts.logLevel.value_or(Log::Level::Warning));
#endif

    if (!ValidateOptions(opts))
    {
        Log::Error(L"Argument Error: {}", opts.errorMsg);
        return 1;
    }

    switch (opts.mode)
    {
        case CapsuleMode::Help:
            PrintUsage(argv[0]);
            return 0;

        case CapsuleMode::Error:
            Log::Error(L"Invalid command line");
            PrintUsage(argv[0], stderr);
            return 0;

        case CapsuleMode::Version:
            PrintVersion();
            return 0;

        case CapsuleMode::Add:
            ec = HandleAddRunner(opts);
            if (ec)
            {
                Log::Critical(L"Failed to add file to resources [{}]", ec);
                return 1;
            }
            return 0;

        case CapsuleMode::Remove:
            ec = HandleRemoveRunner(opts);
            if (ec)
            {
                Log::Critical(L"Failed to remove file from resources [{}]", ec);
                return 1;
            }
            return 0;

        case CapsuleMode::Info:
            ec = HandleInfo(opts);
            if (ec)
            {
                Log::Critical(L"Failed to list embedded executables [{}]", ec);
                return 1;
            }
            return 0;

        case CapsuleMode::Get:
            ec = HandleGetRunner(opts);
            if (ec)
            {
                Log::Critical(L"Failed to extract file from resources [{}]", ec);
                return 1;
            }
            return 0;

        case CapsuleMode::Resource: {
            switch (opts.resourceSubcommand)
            {
                case ResourceSubcommand::List:
                    ec = HandleResourceList(opts);
                    if (ec)
                    {
                        Log::Critical(L"Failed to list resources [{}]", ec);
                        return 1;
                    }
                    break;

                case ResourceSubcommand::Get:
                    ec = HandleResourceGet(opts);
                    if (ec)
                    {
                        Log::Critical(L"Failed to get resource [{}]", ec);
                        return 1;
                    }
                    break;

                case ResourceSubcommand::Set:
                    ec = HandleResourceSet(opts);
                    if (ec)
                    {
                        Log::Critical(L"Failed to set resource [{}]", ec);
                        return 1;
                    }
                    break;

                case ResourceSubcommand::Hexdump:
                    ec = HandleResourceHexdump(opts);
                    if (ec)
                    {
                        Log::Critical(L"Failed to hexdump resource [{}]", ec);
                        return 1;
                    }
                    break;

                default:
                    Log::Error(L"Invalid resource subcommand");
                    return 1;
            }
            return 0;
        }

        case CapsuleMode::RunExplicit:
        case CapsuleMode::Run: {
            DWORD childExitCode = 1;
            ec = HandleRun(opts, childExitCode);
            if (ec)
            {
                Log::Critical(L"Failed to deploy executable from resources [{}]", ec);
                fmt::print(stderr, L"\nTo display help use:\n{} capsule --help\n\n", argv[0]);
                return 1;
            }
            return childExitCode;
        }

        default:
            return 1;
    }
}
