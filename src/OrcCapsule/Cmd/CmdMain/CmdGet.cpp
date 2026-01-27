#include "CmdGet.h"

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Utilities/Resource.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

#include "Cmd/Architecture.h"
#include "Cmd/CmdMain/Runner.h"

#include "OrcCapsuleCLI.h"

[[nodiscard]] std::error_code HandleGetRunner(const CapsuleOptions& args)
{
    std::error_code ec;

    if (!args.outputPath)
    {
        Log::Error(L"Missing argument output path");
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (args.architecture == Architecture::None)
    {
        Log::Error(L"Missing architecture option");
        return std::make_error_code(std::errc::invalid_argument);
    }

    auto resourceName = GetRunnerResourceName(args.architecture);
    ec = ExtractResource(resourceName, *args.outputPath, args.force);
    if (ec)
    {
        Log::Error(L"Failed to extract resource (resource: {}, output: {}) [{}]", resourceName, *args.outputPath, ec);
        return ec;
    }

    Log::Info(L"Extracted {} runner to {}", GetArchitectureName(args.architecture), *args.outputPath);
    return {};
}

void ParseGetCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::Get;

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

    // Parse remaining get options
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

bool ValidateGetOptions(CapsuleOptions& opts)
{
    if (opts.architecture == Architecture::None)
    {
        opts.errorMsg = L"Missing architecture for get command. Specify: x64, x64-xp, x86, x86-xp or arm64";
        opts.valid = false;
        return false;
    }

    if (!opts.outputPath.has_value() || opts.outputPath->empty())
    {
        opts.errorMsg = L"Output path required for get command. Use --output or -o";
        opts.valid = false;
        return false;
    }

    // Bounds checking for output path
    if (opts.outputPath->native().size() > 32767)
    {
        opts.errorMsg = L"Output path exceeds maximum length";
        opts.valid = false;
        return false;
    }

    return true;
}

CommandDescriptor GetGetCommandDescriptor()
{
    return {
        CapsuleMode::Get,
        L"get",
        L"   get [x64|x64-xp|x86|x86-xp|arm64] --output|-o <path>\n"
        L"      Extract a binary from the capsule resources\n"
        L"      x64          Extract 64-bit binary\n"
        L"      x86          Extract 32-bit binary\n"
        L"      x64-xp       Extract Windows XP 64-bit binary\n"
        L"      x86-xp       Extract Windows XP 32-bit binary\n"
        L"      arm64        Extract Windows ARM 64-bit binary\n"
        L"      --output|-o  Required: output file path\n",
        ParseGetCommand,
        ValidateGetOptions,
        HandleGetRunner,
    };
}
