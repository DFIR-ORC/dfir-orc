//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "OrcCapsuleCLI.h"

#include <algorithm>
#include <array>
#include <cwctype>
// Windows API for UTF-16 -> UTF-8 conversion
#include <Windows.h>

#include "fmt/format.h"
#include "fmt/std_filesystem.h"
#include "Version.h"

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Strings.h"

#include "Fmt/std_error_code.h"

#include "Cmd/CmdMain/CmdAdd.h"
#include "Cmd/CmdMain/CmdGet.h"
#include "Cmd/CmdMain/CmdRemove.h"
#include "Cmd/CmdMain/CmdInfo.h"
#include "Cmd/CmdMain/CmdRun.h"
#include "Cmd/CmdResource.h"

// ============================================================================
// ARGUMENT NORMALIZATION FUNCTIONS
// ============================================================================

std::optional<std::pair<std::wstring, std::wstring>>
SplitArgumentAtSeparator(std::wstring_view arg, std::wstring_view separators)
{
    // Find the first occurrence of any separator
    size_t sepPos = arg.find_first_of(separators);

    // Security: Validate separator position
    if (sepPos == std::wstring_view::npos)
    {
        return std::nullopt;  // No separator found
    }

    if (sepPos == 0)
    {
        return std::nullopt;  // Separator at start (invalid)
    }

    // Bounds checking: ensure we don't go past the end
    if (sepPos >= arg.size() - 1)
    {
        return std::nullopt;  // Separator at end, no value present
    }

    std::wstring option(arg.substr(0, sepPos));
    std::wstring value(arg.substr(sepPos + 1));

    return std::make_pair(std::move(option), std::move(value));
}

std::vector<std::wstring> NormalizeArguments(const int argc, const wchar_t* argv[])
{
    std::vector<std::wstring> normalized;

    // Security: Validate argc to prevent integer overflow
    if (argc < 0 || static_cast<size_t>(argc) > SIZE_MAX / 2)
    {
        return normalized;
    }

    // Reserve space to avoid reallocations (worst case: all args split)
    normalized.reserve(static_cast<size_t>(argc) * 2);

    // Start from index 0 to include program name
    for (int i = 0; i < argc; ++i)
    {
        // Security: Validate pointer is not null
        if (argv[i] == nullptr)
        {
            continue;
        }

        std::wstring_view arg(argv[i]);

        if (arg.empty() || (arg[0] != L'/' && arg[0] != L'-'))
        {
            normalized.push_back(std::wstring(arg));
            continue;
        }

        // Try to split on '=' or ':'
        auto splitResult = SplitArgumentAtSeparator(arg);

        if (splitResult.has_value())
        {
            // Successfully split - add both parts
            normalized.push_back(std::move(splitResult->first));
            normalized.push_back(std::move(splitResult->second));
        }
        else
        {
            // No separator found - add as-is
            normalized.push_back(std::wstring(arg));
        }
    }

    return normalized;
}

// ============================================================================
// ARGUMENT PARSING HELPER FUNCTIONS
// ============================================================================

bool IsFlag(std::wstring_view arg, std::wstring_view longFlag, std::wstring_view shortFlag)
{
    return EqualsIgnoreCase(arg, longFlag) || (!shortFlag.empty() && EqualsIgnoreCase(arg, shortFlag));
}

std::pair<std::wstring, Architecture> ParsePathWithArchitecture(std::wstring_view input)
{
    size_t colonPos = input.find_last_of(L':');

    if (colonPos == std::wstring_view::npos)
    {
        return {std::wstring(input), Architecture::None};
    }

    std::wstring_view pathPart = input.substr(0, colonPos);
    std::wstring_view archPart = input.substr(colonPos + 1);

    Architecture arch = ParseArchitecture(archPart);

    // If architecture is invalid, treat the whole string as a path
    if (arch == Architecture::None)
    {
        return {std::wstring(input), Architecture::None};
    }

    return {std::wstring(pathPart), arch};
}

// ============================================================================
// COMMAND PARSING FUNCTIONS
// ============================================================================

bool TryParseGlobalOption(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    // Bounds checking
    if (idx >= args.size())
    {
        return false;
    }

    std::wstring_view arg = args[idx];

    // --no-relocate or /norelocate
    if (IsFlag(arg, L"--no-relocate") || IsFlag(arg, L"/norelocate"))
    {
        opts.relocate = false;
        idx++;
        return true;
    }

    // --no-wait or /nowait
    if (IsFlag(arg, L"--no-wait") || IsFlag(arg, L"/nowait") || IsFlag(arg, L"-nowait"))
    {
        opts.wait = false;
        idx++;
        return true;
    }

    // --temp-dir <path> or /tempdir <path>
    // Note: The normalization step already split "--temp-dir=path" into "--temp-dir" "path"
    if (IsFlag(arg, L"--temp-dir") || IsFlag(arg, L"/tempdir"))
    {
        if (idx + 1 < args.size())
        {
            opts.tempDir = args[idx + 1];
            idx += 2;
            return true;
        }
        idx++;
        return true;
    }

    if (IsFlag(arg, L"--safe-temp") || IsFlag(arg, L"/safetemp"))
    {
        if (idx + 1 < args.size())
        {
            opts.safeTemp = args[idx + 1];
            idx += 2;
            return true;
        }
        idx++;
        return true;
    }

    // --debug or /debug
    if (IsFlag(arg, L"--debug") || IsFlag(arg, L"/debug"))
    {
        opts.logLevel = Log::Level::Debug;
        idx++;
        return true;
    }

    // --force or -f
    if (IsFlag(arg, L"--force", L"-f"))
    {
        opts.force = true;
        idx++;
        return true;
    }

    return false;
}

void ParseExecutionMode(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::Run;

    while (idx < args.size())
    {
        if (TryParseGlobalOption(idx, args, opts))
        {
            continue;
        }

        // Everything else goes to child
        opts.normalizedArgs.push_back(args[idx]);
        idx++;
    }
}

// ============================================================================
// COMMAND REGISTRY
// ============================================================================

// clang-format off
static const std::array<CommandDescriptor, 6> kCommands = {
    GetAddCommandDescriptor(),
    GetGetCommandDescriptor(),
    GetInfoCommandDescriptor(),
    GetRemoveCommandDescriptor(),
    GetResourceCommandDescriptor(),
    GetExecuteCommandDescriptor(),
};
// clang-format on

CapsuleOptions ParseCommandLine(const int argc, const wchar_t* argv[])
{
    CapsuleOptions opts;

    for (size_t i = 1; i < argc; ++i)
    {
        opts.rawArgs.push_back(argv[i]);
    }

    std::vector<std::wstring> args = NormalizeArguments(argc, argv);

    size_t idx = 1;  // Skip program name at index 0

    if (idx >= args.size())
    {
        opts.mode = CapsuleMode::Run;
        return opts;
    }

    // Check for "capsule" keyword
    bool hasCapsuleKeyword = false;
    if (EqualsIgnoreCase(args[idx], L"capsule"))
    {
        hasCapsuleKeyword = true;
        idx++;
    }
    else
    {
        opts.mode = CapsuleMode::Run;
        ParseExecutionMode(idx, args, opts);
        return opts;
    }

    if (idx >= args.size())
    {
        opts.mode = CapsuleMode::Help;
        return opts;
    }

    std::wstring_view firstArg = args[idx];

    // Version command (can be with or without capsule keyword)
    if (IsFlag(firstArg, L"--version", L"-v"))
    {
        opts.mode = CapsuleMode::Version;
        return opts;
    }

    // Help command (can be with or without capsule keyword)
    if (IsFlag(firstArg, L"--help", L"-h"))
    {
        opts.mode = CapsuleMode::Help;
        return opts;
    }

    // If we have the capsule keyword, expect a subcommand
    if (hasCapsuleKeyword)
    {
        for (const auto& cmd : kCommands)
        {
            if (EqualsIgnoreCase(firstArg, cmd.name))
            {
                idx++;
                cmd.parse(idx, args, opts);
                return opts;
            }
        }

        opts.mode = CapsuleMode::Error;
        return opts;
    }

    // No capsule keyword: implicit execution mode
    ParseExecutionMode(idx, args, opts);
    return opts;
}

// ============================================================================
// VALIDATION FUNCTIONS
// ============================================================================


bool ValidateOptions(CapsuleOptions& opts)
{
    for (const auto& cmd : kCommands)
    {
        if (cmd.mode == opts.mode)
        {
            return cmd.validate ? cmd.validate(opts) : true;
        }
    }

    // Modes not in the table (Help, Version, Error, Execution) are always valid.
    return true;
}

// ============================================================================
// HELP/USAGE PRINTING FUNCTIONS
// ============================================================================

void PrintVersion()
{
    fmt::print(L"DFIR-ORC Capsule Version: {}", ORC_VERSION_STRINGW);
}

void PrintUsageHeader(std::wstring_view binaryName, FILE* stream)
{
    // Print the main usage line with common options
    fmt::print(
        stream,
        L"Usage: {} [--version] [--help] [--no-relocate] [--temp-dir=<path>]\n"
        L"           [--debug] <command> [<args>]\n\n",
        binaryName);
}

void PrintUsage(std::wstring_view binaryName, FILE* stream)
{
    PrintUsageHeader(binaryName, stream);

    fmt::print(
        stream,
        L"\nThese are common OrcCapsule commands:\n"
        L"\n"
        L"manage binary resources\n"
        L"   add       Add a binary to the capsule resource section\n"
        L"   get       Extract a binary from the capsule resources\n"
        L"   info      List all executables embedded in the capsule resources\n"
        L"   remove    Remove a binary from the capsule resources\n"
        L"\n"
        L"manage arbitrary resources\n"
        L"   resource  Manage arbitrary Win32 resources (list, get, set)\n"
        L"\n"
        L"execute packaged binaries\n"
        L"   execute   Extract and run the embedded binary\n"
        L"\n"
        L"Command details:\n"
        L"\n");

    for (const auto& cmd : kCommands)
    {
        if (cmd.usage)
        {
            fmt::print(stream, L"{}\n", cmd.usage);
        }
    }

    fmt::print(
        stream,
        L"Global options:\n"
        L"   --version, -v          Display version information\n"
        L"   --help, -h             Display this help message\n"
        L"   --no-relocate          Disable network relocation\n"
        L"   --temp-dir <path>      Set temporary directory\n"
        L"   --debug                Display debug log level\n"
        L"\n"
        L"Note:\n"
        L"   If WOLFLAUNCHER_CONFIG resource exists, executing without\n"
        L"   a command it will run DFIR-ORC with the embedded configuration.\n");
}
