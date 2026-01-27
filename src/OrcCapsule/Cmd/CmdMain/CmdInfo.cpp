#include "CmdInfo.h"

#include <array>

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Utilities/Crypto.h"
#include "Utilities/Compression.h"
#include "Utilities/Process.h"
#include "Utilities/Resource.h"
#include "Utilities/Format.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

#include "Cmd/CmdMain/Runner.h"

#include "OrcCapsuleCLI.h"

namespace {

// TODO: generic ?
[[nodiscard]] std::error_code ProbeRunnerSlot(const std::wstring& resourceName, bool& present, DWORD& compressedSize)
{
    present = false;

    HRSRC hResource = FindResourceW(::GetModuleHandleW(nullptr), resourceName.c_str(), RT_RCDATA);
    if (!hResource)
    {
        auto ec = LastWin32Error();
        if (ec.value() == ERROR_RESOURCE_NAME_NOT_FOUND)
        {
            return {};
        }

        Log::Debug(L"Failed FindResourceW (resource: {}) [{}]", resourceName, ec);
        return ec;
    }

    compressedSize = SizeofResource(::GetModuleHandleW(nullptr), hResource);
    if (compressedSize == 0)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed SizeOfResource (resource: {}) [{}]", resourceName, ec);
        return ec;
    }

    present = true;
    return {};
}

struct RunnerSlotInfo
{
    bool present = false;
    std::wstring resourceName;
    std::wstring extractName;
    std::wstring archName;
    std::optional<uint64_t> compressedSize;
    std::optional<uint64_t> decompressedSize;
    std::optional<std::wstring> sha256;
};

[[nodiscard]] std::error_code GetRunnerSlotInfo(Architecture arch, RunnerSlotInfo& slotInfo)
{
    std::error_code ec;

    slotInfo = {};

    const wchar_t* resourceName = GetRunnerResourceName(arch);
    slotInfo.resourceName = resourceName;

    const auto archName = GetArchitectureName(arch);
    slotInfo.archName = archName;

    ec = GetRunnerFilename(arch, slotInfo.extractName);
    if (ec)
    {
        Log::Debug(L"Failed to extract file name (arch: {}) [{}]", archName, ec);
        return ec;
    }

    DWORD compressedSize = 0;
    ec = ProbeRunnerSlot(resourceName, slotInfo.present, compressedSize);
    if (ec)
    {
        Log::Debug(L"Failed to probe resource (name: {}) [{}]", resourceName, ec);
        return ec;
    }

    if (!slotInfo.present)
    {
        return {};
    }

    slotInfo.compressedSize = compressedSize;

    std::vector<uint8_t> data;
    ec = LoadResourceData(::GetModuleHandleW(nullptr), resourceName, RT_RCDATA, data);
    if (ec)
    {
        Log::Debug(L"Failed LoadResourceData for slot {} [{}]", resourceName, ec);
        return ec;
    }

    std::vector<uint8_t> decompressedData;
    ec = ZstdDecompressData(data, decompressedData);
    if (ec)
    {
        Log::Debug(L"Failed DecompressData for slot {} [{}]", resourceName, ec);
        return ec;
    }

    slotInfo.decompressedSize = static_cast<uint64_t>(decompressedData.size());

    std::wstring sha256;
    ec = ComputeSha256(decompressedData, sha256);
    if (ec)
    {
        Log::Debug(L"Failed CalculateSHA256 for slot {} [{}]", resourceName, ec);
        return ec;
    }

    slotInfo.sha256 = std::move(sha256);
    return {};
}

void PrintRunnerSlotInfo(Architecture arch, const RunnerSlotInfo& slotInfo)
{
    std::wstring error(L"<error>");

    std::optional<std::wstring> ratio;
    if (slotInfo.compressedSize && slotInfo.decompressedSize)
    {
        auto ratioValue =
            (static_cast<double>(*slotInfo.compressedSize) / static_cast<double>(*slotInfo.decompressedSize)) * 100.0;
        ratio = fmt::format(L"({:.2f}%)", ratioValue);
    }

    std::optional<std::wstring> compressedSize;
    if (slotInfo.compressedSize)
    {
        compressedSize = FormatHumanSize(*slotInfo.compressedSize);
    }

    std::optional<std::wstring> decompressedSize;
    if (slotInfo.decompressedSize)
    {
        decompressedSize = FormatHumanSize(*slotInfo.decompressedSize);
    }

    if (slotInfo.present)
    {
        fmt::print(stdout, L"{:>8}[{}] {}\n", L' ', slotInfo.resourceName, slotInfo.extractName);
        fmt::print(
            stdout,
            L"{:>12}Size: {} -> {} {}\n",
            L' ',
            decompressedSize.value_or(error),
            compressedSize.value_or(error),
            ratio.value_or(error));
        fmt::print(L"{:>12}Sha256: {}\n\n", L' ', slotInfo.sha256.value_or(error));
    }
    else
    {
        fmt::print(stdout, L"{:>8}[{}] <Empty>\n\n", L' ', slotInfo.resourceName);
    }
}

}  // namespace

[[nodiscard]] std::error_code HandleInfo(const CapsuleOptions& /* opts */)
{
    constexpr std::array<Architecture, 5> kAllArchitectures = {
        Architecture::X64, Architecture::X86, Architecture::X64_XP, Architecture::X86_XP, Architecture::ARM64};

    fs::path currentExe;
    std::error_code ec = GetCurrentProcessExecutable(currentExe);
    if (ec)
    {
        Log::Error(L"Failed GetCurrentProcessExecutable [{}]", ec);
        return ec;
    }

    auto capsuleSize = static_cast<uint64_t>(fs::file_size(currentExe, ec));
    if (ec)
    {
        Log::Debug(L"Failed file_size (path: {}) [{}]", currentExe, ec);
        capsuleSize = 0;
        ec.clear();
    }

    std::wstring capsuleSha256;
    ec = ComputeSha256(currentExe, capsuleSha256);
    if (ec)
    {
        Log::Debug(L"Failed CalculateSHA256 (path: {}) [{}]", currentExe, ec);
        capsuleSha256 = L"<error>";
        ec.clear();
    }

    fmt::print(
        stdout,
        LR"(
Capsule: {}
    Size: {} ({})
    Sha256: {}

    Embedded executables:

)",
        currentExe,
        FormatHumanSize(capsuleSize),
        FormatRawBytes(capsuleSize),
        capsuleSha256);

    for (Architecture arch : kAllArchitectures)
    {
        RunnerSlotInfo slotInfo;
        ec = GetRunnerSlotInfo(arch, slotInfo);
        if (ec)
        {
            if (ec.value() != ERROR_RESOURCE_NOT_FOUND && ec.value() != ERROR_RESOURCE_TYPE_NOT_FOUND)
            {
                Log::Error(L"Failed to load resource slot info for {} [{}]", GetArchitectureName(arch), ec);
                continue;
            }
        }

        PrintRunnerSlotInfo(arch, slotInfo);
    }

    fmt::print(stdout, L"\n");
    return {};
}

// ParseInfoCommand sets the mode to Info.
// The 'info' subcommand takes no arguments: it simply enumerates all
// architecture slots embedded in the capsule resource section.
// Any trailing global options (e.g. --debug) are still consumed so the
// parser stays consistent with the rest of the command family.
void ParseInfoCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::Info;

    // Consume any trailing global options; anything else is silently ignored
    // (there are no info-specific flags today, but this keeps the parser
    // consistent and forward-compatible).
    while (idx < args.size())
    {
        if (!TryParseGlobalOption(idx, args, opts))
        {
            // Unknown trailing argument — skip it rather than hard-failing so
            // that 'capsule info --debug' and plain 'capsule info' both work.
            idx++;
        }
    }
}

CommandDescriptor GetInfoCommandDescriptor()
{
    return {
        CapsuleMode::Info,
        L"info",
        L"   info\n"
        L"      List all executables embedded in the capsule resources\n"
        L"      Prints each architecture slot that is currently present\n",
        ParseInfoCommand,
        nullptr,
        HandleInfo,
    };
}
