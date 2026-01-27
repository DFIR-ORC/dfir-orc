#include "CmdRemove.h"

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Utilities/Resource.h"
#include "Utilities/Process.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

#include "Cmd/CmdMain/Runner.h"
#include "OrcCapsuleCLI.h"

[[nodiscard]] std::error_code HandleRemoveRunner(const CapsuleOptions& args)
{
    std::error_code ec;

    if (args.architecture == Architecture::None)
    {
        Log::Error(L"Missing architecture option");
        return std::make_error_code(std::errc::invalid_argument);
    }

    fs::path currentExe;
    ec = GetCurrentProcessExecutable(currentExe);
    if (ec)
    {
        Log::Error(L"Failed GetCurrentProcessExecutable [{}]", ec);
        return ec;
    }

    auto resourceName = GetRunnerResourceName(args.architecture);
    HRSRC resourceInfo = FindResourceW(::GetModuleHandleW(NULL), resourceName, RT_RCDATA);
    if (!resourceInfo)
    {
        ec = LastWin32Error();
        if (ec.value() == ERROR_RESOURCE_NOT_FOUND || ec.value() == ERROR_RESOURCE_TYPE_NOT_FOUND)
        {
            Log::Info(L"Resource for {} not found", GetArchitectureName(args.architecture));
            return {};
        }

        Log::Debug(L"Failed FindResourceW (resource: {}, path: {}) [{}]", resourceName, currentExe, ec);
        return ec;
    }

    fs::path updatedExe =
        args.outputPath.value_or(currentExe.parent_path() / (currentExe.filename().wstring() + L".new"));

    fs::copy_file(currentExe, updatedExe, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        Log::Error(L"Failed fs::copy_file (input: {}, output: {}) [{}]", currentExe, updatedExe, ec);
        return ec;
    }

    ec = RemoveResource(updatedExe, resourceName, RT_RCDATA);
    if (ec)
    {
        Log::Error(L"Failed UpdateResourceData (path: {}, resource: {}) [{}]", updatedExe, resourceName, ec);
        return std::make_error_code(std::errc::bad_message);
    }

    if (!args.outputPath)
    {
        ec = PerformSelfUpdate(updatedExe);
        if (ec)
        {
            Log::Error(
                L"Failed file swapping to perform self update (original: {}, updated: {}) [{}]",
                currentExe,
                updatedExe,
                ec);
        }

        std::swap(currentExe, updatedExe);
    }

    Log::Info(L"Removed {} runner from {}", GetArchitectureName(args.architecture), updatedExe);
    return {};
}

void ParseRemoveCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::Remove;

    // Check for optional architecture argument
    if (idx < args.size())
    {
        Architecture arch = ParseArchitecture(args[idx]);
        if (arch != Architecture::None)
        {
            opts.architecture = arch;
            idx++;
        }
    }

    // Parse remaining remove options
    while (idx < args.size())
    {
        if (TryParseGlobalOption(idx, args, opts))
        {
            continue;
        }

        std::wstring_view arg = args[idx];

        // --output or -o or /output
        if (IsFlag(arg, L"--output", L"-o") || IsFlag(arg, L"/output"))
        {
            if (idx + 1 < args.size())
            {
                opts.outputPath = args[idx + 1];
                idx += 2;
                continue;
            }
        }

        idx++;
    }
}

bool ValidateRemoveOptions(CapsuleOptions& opts)
{
    if (opts.architecture == Architecture::None)
    {
        opts.errorMsg = L"Missing architecture for remove command. Specify: x64, x64-xp, x86, x86-xp or arm64";
        opts.valid = false;
        return false;
    }

    return true;
}

CommandDescriptor GetRemoveCommandDescriptor()
{
    return {
        CapsuleMode::Remove,
        L"remove",
        L"   remove <x64|x64-xp|x86|x86-xp|arm64> [--output|-o <path>]\n"
        L"      Remove a binary from the capsule resources\n"
        L"      x64          Remove 64-bit binary\n"
        L"      x86          Remove 32-bit binary\n"
        L"      x64-xp       Remove Windows XP 64-bit binary\n"
        L"      x86-xp       Remove Windows XP 32-bit binary\n"
        L"      arm64        Remove Windows ARM 64-bit binary\n"
        L"      --output|-o  Output path (default: self-update)\n",
        ParseRemoveCommand,
        ValidateRemoveOptions,
        HandleRemoveRunner,
    };
}
