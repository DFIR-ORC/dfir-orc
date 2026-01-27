#include "CmdAdd.h"

#include <array>

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Utilities/Compression.h"
#include "Utilities/Crypto.h"
#include "Utilities/File.h"
#include "Utilities/Process.h"
#include "Utilities/Resource.h"
#include "Utilities/Format.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

#include "Cmd/Architecture.h"
#include "Cmd/CmdMain/Runner.h"
#include "resource.h"
#include "OrcCapsuleCLI.h"

// Forward declarations (definitions follow HandleAddRunner below)
void ParseAddCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts);
bool ValidateAddOptions(CapsuleOptions& opts);

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#    define PROCESSOR_ARCHITECTURE_ARM64 kPROCESSOR_ARCHITECTURE_ARM64
#endif

#ifndef IMAGE_FILE_MACHINE_ARM64
#    define IMAGE_FILE_MACHINE_ARM64 kIMAGE_FILE_MACHINE_ARM64
#endif

namespace {

[[nodiscard]] std::error_code GetTargetArchitectureFromPeFile(const fs::path& path, Architecture& architecture)
{
    std::error_code ec;

    ScopedFileHandle handle(CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL));

    if (!handle)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed CreateFileW (path: {}) [{}]", path, ec);
        return ec;
    }

    IMAGE_DOS_HEADER dosHeader;
    ec = ReadFileAt(handle.get(), 0, &dosHeader, sizeof(dosHeader));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for IMAGE_DOS_HEADER (path: {}) [{}]", path, ec);
        return ec;
    }

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
    {
        Log::Debug(L"Invalid PE file magic (actual: {}, expected: {})", dosHeader.e_magic, IMAGE_DOS_SIGNATURE);
        return std::make_error_code(std::errc::bad_message);
    }

    DWORD signature = 0;
    ec = ReadFileAt(handle.get(), dosHeader.e_lfanew, &signature, sizeof(signature));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for IMAGE_NT_SIGNATURE (path: {}) [{}]", path, ec);
        return ec;
    }

    if (signature != IMAGE_NT_SIGNATURE)
    {
        Log::Debug(L"Invalid PE file signature (actual: {}, expected: {})", signature, IMAGE_NT_SIGNATURE);
        return std::make_error_code(std::errc::bad_message);
    }

    IMAGE_FILE_HEADER fileHeader;
    ec = ReadFileAt(handle.get(), dosHeader.e_lfanew + sizeof(DWORD), &fileHeader, sizeof(fileHeader));
    if (ec)
    {
        Log::Debug(L"Failed ReadFileAt for IMAGE_FILE_HEADER (path: {}) [{}]", path, ec);
        return ec;
    }

    switch (fileHeader.Machine)
    {
        case IMAGE_FILE_MACHINE_I386:
            architecture = Architecture::X86;
            break;
        case IMAGE_FILE_MACHINE_AMD64:
            architecture = Architecture::X64;
            break;
        case IMAGE_FILE_MACHINE_ARM64:
            architecture = Architecture::ARM64;
            break;
        default:
            return std::make_error_code(std::errc::bad_message);
    }

    return {};
}

[[nodiscard]] std::error_code GetTargetArchitectureFromResourceString(const fs::path& path, Architecture& architecture)
{
    std::error_code ec;

    ScopedModule module(
        LoadLibraryExW(path.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    if (!module)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed LoadLibraryExW (path: {}) [{}]", path, ec);
        return ec;
    }

    std::wstring value;
    ec = GetResourceString(module.get(), IDS_COMPATIBILITY, value);
    if (ec)
    {
        Log::Debug(L"Failed to detect architecture from resources (path: {}) [{}]", path, ec);
        return ec;
    }

    architecture = ParseArchitecture(value);
    if (architecture == Architecture::None)
    {
        Log::Debug(L"Failed to recognize compatibility string (value: {})", value);
        return std::make_error_code(std::errc::bad_message);
    }

    return {};
}

[[nodiscard]] std::error_code GetExecutableTargetArchitecture(const fs::path& path, Architecture& architecture)
{
    std::error_code ec;

    ec = GetTargetArchitectureFromResourceString(path, architecture);
    if (!ec)
    {
        return {};
    }

    if (ec == std::errc::no_such_file_or_directory)
    {
        return ec;
    }

    ec = GetTargetArchitectureFromPeFile(path, architecture);
    if (!ec)
    {
        return {};
    }

    Log::Debug(L"Failed to detect architecture (path: {})", path);
    return std::make_error_code(std::errc::invalid_argument);
}

struct PreparedRunner
{
    fs::path inputPath;
    Architecture architecture = Architecture::None;
    std::wstring resourceName;
    uint64_t rawDataSize = 0;
    std::vector<uint8_t> compressedData;
    std::wstring rawDataSha256;
    std::wstring extractedFilename;
};

[[nodiscard]] std::error_code
PrepareRunner(const fs::path& inputPath, Architecture architectureHint, PreparedRunner& out)
{
    std::error_code ec;

    Log::Debug(L"Add '{}'", inputPath);

    Architecture architecture = architectureHint;
    if (architecture == Architecture::None)
    {
        ec = GetExecutableTargetArchitecture(inputPath, architecture);
        if (ec)
        {
            Log::Debug(L"Failed to detect architecture, specify it with an option trail :x64, :x86, :x86-xp, :arm64");
            return ec;
        }

        Log::Debug(L"Detected architecture: {}", GetArchitectureName(architecture));
    }

    std::vector<uint8_t> rawData;
    ec = ReadFileContents(inputPath, rawData);
    if (ec)
    {
        Log::Debug(L"Failed ReadFileContents (path: {}) [{}]", inputPath, ec);
        return ec;
    }

    Log::Debug(L"Compressing {} bytes", rawData.size());

    std::vector<uint8_t> compressedData;
    ec = ZstdCompressData(rawData, compressedData);
    if (ec)
    {
        Log::Debug(L"Failed CompressData (input: {}) [{}]", inputPath, ec);
        return ec;
    }

    Log::Debug(L"Compressed size {} bytes", compressedData.size());

    std::wstring rawDataSha256 = L"<error>";
    ec = ComputeSha256(rawData, rawDataSha256);
    if (ec)
    {
        Log::Debug(L"Failed SHA256 on raw data (path: {}) [{}]", inputPath, ec);
        ec.clear();
    }

    std::wstring extractedFilename = L"<error>";
    ec = GetRunnerFilename(architecture, extractedFilename);
    if (ec)
    {
        ec.clear();
    }

    out.inputPath = inputPath;
    out.architecture = architecture;
    out.resourceName = GetRunnerResourceName(architecture);
    out.rawDataSize = rawData.size();
    out.compressedData = std::move(compressedData);
    out.rawDataSha256 = std::move(rawDataSha256);
    out.extractedFilename = std::move(extractedFilename);

    return {};
}

[[nodiscard]] std::error_code
ApplyPreparedRunners(const fs::path& targetExe, const std::vector<PreparedRunner>& executables)
{
    std::unique_ptr<ResourceUpdater> updater;
    auto ec = ResourceUpdater::Make(targetExe, updater);
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Make (path: {}) [{}]", targetExe, ec);
        return ec;
    }

    for (const auto& exe : executables)
    {
        ec =
            updater->Update(RT_RCDATA, exe.resourceName, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), exe.compressedData);
        if (ec)
        {
            Log::Debug(
                L"Failed ResourceUpdater::Update (path: {}, resource: {}) [{}]", targetExe, exe.resourceName, ec);
            return ec;
        }

        Log::Debug(L"Staged resource: {} -> {}", exe.inputPath, exe.resourceName);
    }

    ec = updater->Commit();
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Commit (path: {}) [{}]", targetExe, ec);
    }

    return ec;
}

void PrintAddedRunnerSummary(const PreparedRunner& exe)
{
    const double ratio = (exe.rawDataSize > 0)
        ? (static_cast<double>(exe.compressedData.size()) / static_cast<double>(exe.rawDataSize)) * 100.0
        : 0.0;

    fmt::print(
        stdout,
        L"\n"
        L"        [{}]\n"
        L"            Path:     {}\n"
        L"            Size:     {} -> {} ({:.2f}%)\n"
        L"            Sha256:   {}\n"
        L"            Platform: {}\n",
        exe.resourceName,
        exe.inputPath,
        FormatHumanSize(exe.rawDataSize),
        FormatHumanSize(exe.compressedData.size()),
        ratio,
        exe.rawDataSha256,
        GetArchitectureName(exe.architecture));
}

}  // namespace

[[nodiscard]] std::error_code HandleAddRunner(const CapsuleOptions& args)
{
    std::error_code ec;

    if (args.inputPaths.empty())
    {
        Log::Error(L"No input path specified");
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (args.inputPaths.size() > ArchitectureCount())
    {
        Log::Error(L"Too many input paths: {} (maximum is {})", args.inputPaths.size(), ArchitectureCount());
        return std::make_error_code(std::errc::invalid_argument);
    }

    std::vector<PreparedRunner> executables;
    executables.reserve(args.inputPaths.size());

    for (const auto& inputPath : args.inputPaths)
    {
        PreparedRunner prepared;
        ec = PrepareRunner(inputPath, args.architecture, prepared);
        if (ec)
        {
            Log::Error(L"Failed to prepare executable (path: {}) [{}]", inputPath, ec);
            return ec;
        }

        executables.push_back(std::move(prepared));
    }

    for (size_t i = 0; i < executables.size(); ++i)
    {
        for (size_t j = i + 1; j < executables.size(); ++j)
        {
            if (executables[i].resourceName == executables[j].resourceName)
            {
                Log::Error(
                    L"Duplicate architecture slot '{}': '{}' and '{}' both target the same resource",
                    executables[i].resourceName,
                    executables[i].inputPath,
                    executables[j].inputPath);
                return std::make_error_code(std::errc::invalid_argument);
            }
        }
    }

    fs::path currentExe;
    ec = GetCurrentProcessExecutable(currentExe);
    if (ec)
    {
        Log::Error(L"Failed GetCurrentProcessExecutable [{}]", ec);
        return ec;
    }

    fs::path updatedExe =
        args.outputPath.value_or(currentExe.parent_path() / (currentExe.filename().wstring() + L".new"));

    fs::copy_file(
        currentExe, updatedExe, args.force ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
    if (ec)
    {
        Log::Error(L"Failed fs::copy_file (src: {}, dst: {}) [{}]", currentExe, updatedExe, ec);
        return ec;
    }

    ec = ApplyPreparedRunners(updatedExe, executables);
    if (ec)
    {
        Log::Error(L"Failed to apply resource updates (path: {}) [{}]", updatedExe, ec);
        return ec;
    }

    std::optional<std::wstring> expectedSha256;
    if (!args.outputPath)
    {
        expectedSha256.emplace();
        ec = ComputeSha256(updatedExe, *expectedSha256);
        if (ec)
        {
            Log::Error(L"Failed CalculateSHA256 (path: {}) [{}]", updatedExe, ec);
            return ec;
        }

        ec = PerformSelfUpdate(updatedExe);
        if (ec)
        {
            Log::Error(L"Failed self-update swap (original: {}, updated: {}) [{}]", updatedExe, ec);
            return ec;
        }

        std::swap(currentExe, updatedExe);
    }

    std::wstring updatedExeSha256;
    ec = ComputeSha256(updatedExe, updatedExeSha256);
    if (ec)
    {
        Log::Debug(L"Failed CalculateSHA256 (path: {}) [{}]", updatedExe, ec);
        return ec;
    }

    if (expectedSha256 && *expectedSha256 != updatedExeSha256)
    {
        Log::Error(L"Hash mismatch after self-update swap (original: {}, updated: {})", currentExe, updatedExe);
        return std::make_error_code(std::errc::bad_message);
    }

    auto updatedExeSize = static_cast<uint64_t>(fs::file_size(updatedExe, ec));
    if (ec)
    {
        Log::Error(L"Failed file_size (path: {}) [{}]", updatedExe, ec);
        ec.clear();
    }

    fmt::print(
        stdout,
        L"\nUpdated Capsule: {}\n"
        L"    Size:   {} ({})\n"
        L"    Sha256: {}\n"
        L"\n"
        L"    Added executables:\n",
        updatedExe,
        FormatHumanSize(updatedExeSize),
        FormatRawBytes(updatedExeSize),
        updatedExeSha256);

    for (const auto& exe : executables)
    {
        PrintAddedRunnerSummary(exe);
    }

    fmt::print(stdout, L"\n");
    return {};
}

CommandDescriptor GetAddCommandDescriptor()
{
    return {
        CapsuleMode::Add,
        L"add",
        L"   add <path>[:x64|:x64-xp|:x86|:x86-xp|:arm64] [<path2>[:arch]] [<path3>[:arch]] [--output|-o <path>]\n"
        L"      Add up to 3 binary files to the capsule resources (one per architecture slot)\n"
        L"      <path>       Path to the binary file to add (repeated up to 3 times)\n"
        L"      :x64         Add as 64-bit binary (auto-detected if omitted)\n"
        L"      :x86         Add as 32-bit binary (auto-detected if omitted)\n"
        L"      :x64-xp      Add as Windows XP 64-bit binary (auto-detected if omitted)\n"
        L"      :x86-xp      Add as Windows XP 32-bit binary (auto-detected if omitted)\n"
        L"      :arm64       Add as Windows ARM 64-bit binary (auto-detected if omitted)\n",
        ParseAddCommand,
        ValidateAddOptions,
        HandleAddRunner,
    };
}

void ParseAddCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::Add;

    // Consume up to kMaxInputPaths positional path tokens (e.g. "file.exe" or "file.exe:x64").
    // A token is positional when it does not start with '-' or '/'.
    // Per-token architecture suffixes set opts.architecture (last suffix wins).
    // For multi-file adds, rely on auto-detection and omit the suffix.
    while (idx < args.size() && opts.inputPaths.size() < ArchitectureCount())
    {
        std::wstring_view arg = args[idx];

        if (arg.empty() || arg[0] == L'-' || arg[0] == L'/')
        {
            break;  // Reached a flag — stop collecting paths
        }

        auto [path, arch] = ParsePathWithArchitecture(arg);
        opts.inputPaths.push_back(std::move(path));
        if (arch != Architecture::None)
        {
            opts.architecture = arch;
        }

        idx++;
    }

    // Parse remaining add options (flags and --output)
    while (idx < args.size())
    {
        if (TryParseGlobalOption(idx, args, opts))
        {
            continue;
        }

        std::wstring_view arg = args[idx];

        // --output or -o or /output
        // Note: Normalization already handled "--output=path" -> "--output" "path"
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

bool ValidateAddOptions(CapsuleOptions& opts)
{
    constexpr size_t kMaxPathLength = 32767;  // MAX_PATH extended limit

    if (opts.inputPaths.empty())
    {
        opts.errorMsg = L"Input path required for add command";
        opts.valid = false;
        return false;
    }

    if (opts.inputPaths.size() > ArchitectureCount())
    {
        opts.errorMsg =
            fmt::format(L"Too many input paths: {} (maximum is {})", opts.inputPaths.size(), ArchitectureCount());
        opts.valid = false;
        return false;
    }

    for (const auto& path : opts.inputPaths)
    {
        if (path.native().size() > kMaxPathLength)
        {
            opts.errorMsg = fmt::format(L"Input path exceeds maximum length: {}", path);
            opts.valid = false;
            return false;
        }
    }

    return true;
}
