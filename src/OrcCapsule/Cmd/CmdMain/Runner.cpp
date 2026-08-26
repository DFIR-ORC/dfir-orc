#include "Runner.h"

#include <array>

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Utilities/File.h"
#include "Utilities/Process.h"
#include "Utilities/Resource.h"
#include "Utilities/Scope.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"
#include "Cmd/Architecture.h"
#include "OrcCapsuleCli.h"

namespace fs = std::filesystem;

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#    define PROCESSOR_ARCHITECTURE_ARM64 kPROCESSOR_ARCHITECTURE_ARM64
#endif

#ifndef IMAGE_FILE_MACHINE_ARM64
#    define IMAGE_FILE_MACHINE_ARM64 kIMAGE_FILE_MACHINE_ARM64
#endif

namespace {

[[nodiscard]] bool GetOSVersionInfo(OSVERSIONINFOEXW& osvi)
{
    osvi = {sizeof(osvi)};
#pragma warning(push)
#pragma warning(disable : 4996)
    return GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&osvi)) != 0;
#pragma warning(pop)
}

// NT 5.1 = XP, 5.2 = XP x64/Server 2003, 6.0 = Vista/Server 2008
[[nodiscard]] bool IsLegacyFamily(const OSVERSIONINFOEXW& osvi)
{
    return (osvi.dwMajorVersion == 5) || (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0);
}

[[nodiscard]] Architecture GetNativeSystemArchitecture()
{
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);

    switch (sysInfo.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return Architecture::X64;
        case PROCESSOR_ARCHITECTURE_INTEL:
            return Architecture::X86;
        case PROCESSOR_ARCHITECTURE_ARM64:
            return Architecture::ARM64;
        default:
            return Architecture::X64;
    }
}

}  // namespace

const wchar_t* GetRunnerResourceName(Architecture arch)
{
    switch (arch)
    {
        case Architecture::X64:
            return L"RUN64";
        case Architecture::X86:
            return L"RUN32";
        case Architecture::X64_XP:
            return L"RUN64XP";
        case Architecture::X86_XP:
            return L"RUN32XP";
        case Architecture::ARM64:
            return L"RUNARM64";
        default:
            return L"<unknown>";
    }
}

[[nodiscard]] std::error_code GetRunnerFilename(Architecture arch, std::wstring& filename)
{
    fs::path currentExe;
    auto ec = GetCurrentProcessExecutable(currentExe);
    if (ec)
    {
        Log::Debug(L"Failed GetCurrentProcessExecutable [{}]", ec);
        return ec;
    }

    filename = currentExe.stem().wstring() + L"_" + GetArchitectureName(arch) + L".exe";
    return {};
}

// Extract the runner for @p arch into a delete-on-close file. On success @p outputFile holds its path and
// @p keepAlive holds the open handle that must stay open until the runner has exited (closing it removes
// the file). On failure both are left untouched.
[[nodiscard]] std::error_code ExtractRunner(
    const std::wstring& outputDirectory,
    Architecture arch,
    fs::path& outputFile,
    ScopedFileHandle& keepAlive)
{
    const auto* resourceName = GetRunnerResourceName(arch);

    std::wstring filename;
    auto ec = GetRunnerFilename(arch, filename);
    if (ec)
    {
        Log::Debug(L"Failed GetRunnerFilename [{}]", ec);
        return ec;
    }

    const auto tempPath = fs::path(outputDirectory) / filename;

    // Write the runner to disk with a short-lived handle, then hold a read/delete handle marked
    // delete-on-close. The keep-alive handle must not carry write access: CreateProcessW's image loader
    // opens the runner without FILE_SHARE_WRITE and a retained writable handle makes the launch fail with
    // ERROR_SHARING_VIOLATION.
    ec = ExtractResource(resourceName, tempPath.wstring(), /*overwrite=*/true);
    if (ec)
    {
        Log::Debug(L"Failed ExtractResource (resource: {}, output: {}) [{}]", resourceName, tempPath, ec);
        return ec;
    }

    ScopedFileHandle handle;
    ec = OpenDeleteOnCloseFile(tempPath, handle);
    if (ec)
    {
        // The runner extracted correctly; only the delete-on-close guard could not be armed. Run it anyway
        // instead of failing the launch: the sole cost is a file left on disk, which HandleRun removes
        // best-effort once the child has exited. An empty keepAlive is the signal that no auto-delete is set.
        Log::Warn(
            L"Could not arm delete-on-close for the runner; it will be cleaned up after the run instead "
            L"(output: {}) [{}]",
            tempPath,
            ec);
    }

    outputFile = tempPath;
    keepAlive = std::move(handle);
    return {};
}

[[nodiscard]] std::error_code
ExtractCompatibleRunner(const std::wstring& outputDirectory, fs::path& outputFile, ScopedFileHandle& keepAlive)
{
    Architecture arch;
    auto ec = ResolveRunnerArchitectureFromHost(arch);
    if (ec)
    {
        Log::Debug("Failed ResolveRunnerArchitectureFromHost [{}]", ec);
        return ec;
    }

    ec = ExtractRunner(outputDirectory, arch, outputFile, keepAlive);
    if (!ec)
    {
        return {};
    }

    constexpr auto architectures = std::array {
        Architecture::ARM64, Architecture::X64, Architecture::X64_XP, Architecture::X86, Architecture::X86_XP};

    size_t i = 0;
    for (i = 0; i < architectures.size(); ++i)
    {
        if (arch == architectures[i])
        {
            break;
        }
    }

    for (size_t j = i + 1; j < architectures.size(); ++j)
    {
        const auto current = architectures[j];

        Log::Debug(L"No runner binary found, fallback on architecture '{}'", GetArchitectureName(current));
        ec = ExtractRunner(outputDirectory, current, outputFile, keepAlive);
        if (!ec)
        {
            Log::Debug(
                L"No exact match found for target architecture. Falling back to '{}'", GetArchitectureName(current));
            return {};
        }
    }

    return std::make_error_code(std::errc::no_such_file_or_directory);
}

[[nodiscard]] std::error_code ResolveRunnerArchitectureFromHost(Architecture& arch)
{
    OSVERSIONINFOEXW osvi;
    if (!GetOSVersionInfo(osvi))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed GetVersionExW [{}]", ec);
        return ec;
    }

    const auto nativeArch = GetNativeSystemArchitecture();

    // Check for the XP/Vista era first
    if (IsLegacyFamily(osvi))
    {
        arch = (nativeArch == Architecture::X64) ? Architecture::X64_XP : Architecture::X86_XP;
    }
    else
    {
        // Modern OS (Windows 7, 8, 10, 11)
        arch = nativeArch;
    }

    return {};
}
