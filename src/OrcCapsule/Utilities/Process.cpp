//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Process.h"

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"
#include "Utilities/File.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::error_code
CopyFileAndMarkForRebootRemoval(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::error_code ec;

    if (!CopyFileW(source.c_str(), destination.c_str(), FALSE))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed CopyFileW (input: {}, output: {}) [{}]", source, destination, ec);
        return ec;
    }

    if (!MoveFileExW(destination.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed MoveFileExW on register for removal (path: {}) [{}]", destination, ec);
        return ec;
    }

    Log::Debug(L"Scheduled file for deletion on reboot (path: {})", destination);

    return {};
}

inline bool StartsAsNetworkPath(const std::wstring& path)
{
    if (path.size() < 2)
    {
        return false;
    }

    return (path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/');
}

// Detect if a path is a network path or a local path.
// Avoid using PathIsNetworkPathW which make a direct dependency on shlwapi.dll and at least an indirect on mpr.dll
[[nodiscard]] std::error_code IsNetworkPath(const std::wstring& path, bool& isNetworkPath)
{
    const size_t pathMinSize = 3;
    if (path.size() < pathMinSize)
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (StartsAsNetworkPath(path))
    {
        isNetworkPath = true;
        return {};
    }

    if (path[1] != L':' || path[2] != L'\\')
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const std::wstring drive = path.substr(0, 3);
    auto type = GetDriveTypeW(drive.c_str());
    isNetworkPath = (type == DRIVE_REMOTE);
    return {};
}

[[nodiscard]] bool ShouldRelocate(const std::wstring& modulePath)
{
    bool isNetwork = true;
    auto ec = IsNetworkPath(modulePath, isNetwork);

    if (ec)
    {
        Log::Warn(L"Cannot validate path type for '{}', assuming local [{}]", modulePath, ec);
        return false;
    }

    if (!isNetwork)
    {
        Log::Debug(L"No relocation needed - running from local drive: {}", modulePath);
    }

    return isNetwork;
}

[[nodiscard]] std::error_code RelocateOrcFiles(
    const std::filesystem::path& modulePath,
    const std::filesystem::path& outputDirectory,
    std::vector<std::wstring>& newFiles)
{
    std::filesystem::path configPath = modulePath;
    configPath.replace_extension(L"xml");

    const auto newConfigPath = outputDirectory / configPath.filename();

    auto ec = CopyFileAndMarkForRebootRemoval(configPath, newConfigPath);
    if (ec == std::errc::no_such_file_or_directory)
    {
        Log::Debug(L"No xml configuration found");
        ec.clear();
    }
    else if (ec)
    {
        Log::Error(L"Failed to relocate config (input: {}, output: {}) [{}]", configPath, newConfigPath, ec);
        return ec;
    }
    else
    {
        newFiles.push_back(newConfigPath);
    }

    const auto newModulePath = outputDirectory / modulePath.filename();
    ec = CopyFileAndMarkForRebootRemoval(modulePath, newModulePath);
    if (ec)
    {
        Log::Error(L"Failed to relocate executable (input: {}, output: {}) [{}]", modulePath, newModulePath, ec);
        return ec;
    }

    newFiles.push_back(newModulePath);
    return {};
}

[[nodiscard]] std::error_code
LaunchRelocatedOrc(const std::filesystem::path& path, const std::vector<std::wstring>& args, PROCESS_INFORMATION& pi)
{
    std::wstring cmdLine = L"\"" + path.native() + L"\"";
    for (const auto& arg : args)
    {
        cmdLine += L" \"" + arg + L"\"";
    }

    cmdLine += L" /norelocate";

    std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back(L'\0');

    SecureZeroMemory(&pi, sizeof(pi));

    STARTUPINFOW si;
    SecureZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcessW(
            path.c_str(),
            cmdBuffer.data(),
            nullptr,  // process security
            nullptr,  // thread security
            FALSE,  // inherit handles
            CREATE_UNICODE_ENVIRONMENT,
            nullptr,  // environment
            nullptr,  // current directory
            &si,
            &pi))
    {
        auto ec = LastWin32Error();
        Log::Error(L"Failed CreateProcessW (path: {}) [{}]", path, ec);
        return ec;
    }

    return {};
}

}  // namespace

std::error_code GetCurrentProcessExecutable(fs::path& path)
{
    std::wstring strPath(MAX_PATH, L'\0');

    DWORD length = GetModuleFileNameW(nullptr, strPath.data(), static_cast<DWORD>(strPath.size()));
    if (length == 0)
    {
        return LastWin32Error();
    }

    strPath.resize(length);
    path = strPath;
    return {};
}

std::error_code
WaitChildProcess(const PROCESS_INFORMATION& pi, const std::filesystem::path& executable, DWORD& childExitCode)
{
    std::error_code ec;
    std::error_code localError;

    ScopedFileHandle hProcess(pi.hProcess);
    ScopedFileHandle hThread(pi.hThread);

    if (WaitForSingleObject(hProcess.get(), INFINITE) == WAIT_FAILED)
    {
        Log::Debug(L"Failed WaitForSingleObject (process executable: {}) [{}]", executable, localError);
        ec = localError;
        localError.clear();  // best outcome is to continue
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess.get(), &exitCode) == 0)
    {
        Log::Debug(L"Failed GetExitCodeProcess (process executable: {}) [{}]", executable, localError);
        ec = localError;
        localError.clear();  // best outcome is to continue
    }

    childExitCode = static_cast<DWORD>(exitCode);
    return ec;
}

[[nodiscard]] std::error_code
RelocateOrcFromNetwork(const std::filesystem::path& outputDirectory, const std::vector<std::wstring>& args)
{
    std::filesystem::path currentPath;
    auto ec = GetCurrentProcessExecutable(currentPath);
    if (ec)
    {
        return ec;
    }

    if (!ShouldRelocate(currentPath))
    {
        return {};
    }

    std::vector<std::wstring> newRelocatedFiles;
    ec = RelocateOrcFiles(currentPath, outputDirectory, newRelocatedFiles);
    if (newRelocatedFiles.empty())
    {
        Log::Debug(L"No files were relocated");
        return std::make_error_code(std::errc::bad_message);
    }

    DWORD childExitCode = 1;

    {
        auto cleanup = MakeScopeGuard([paths = newRelocatedFiles]() noexcept {
            for (const auto& path : paths)
            {
                auto err = RemoveFile(path);
                if (err)
                {
                    Log::Error(L"Failed to schedule file for deletion (path: {}) [{}]", path, err);
                    err.clear();
                }
            }
        });

        if (ec)
        {
            return ec;
        }

        PROCESS_INFORMATION pi;
        const auto relocationPath = newRelocatedFiles.back();
        ec = LaunchRelocatedOrc(relocationPath, args, pi);
        if (ec)
        {
            return ec;
        }

        ec = WaitChildProcess(pi, relocationPath, childExitCode);
        if (ec)
        {
            Log::Error(L"Failed WaitChildProcess (path: {}) [{}]", relocationPath, ec);
            ec.clear();  // best outcome is to continue
        }
    }

    ExitProcess(childExitCode);
}

std::error_code PerformSelfUpdate(const fs::path& newExe)
{
    std::error_code ec;

    std::filesystem::path currentExe;
    ec = GetCurrentProcessExecutable(currentExe);
    if (ec)
    {
        return ec;
    }

    const fs::path backupPath =
        currentExe.parent_path() / (currentExe.stem().wstring() + currentExe.extension().wstring() + L".bck");

    if (!MoveFileExW(currentExe.c_str(), backupPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        ec = LastWin32Error();
        Log::Error(L"Failed MoveFileExW (input: {}, output: {}) [{}]", currentExe, backupPath, ec);
        return ec;
    }

    if (!MoveFileExW(newExe.c_str(), currentExe.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        ec = LastWin32Error();
        Log::Error(L"Failed MoveFileExW (input: {}, output: {}) [{}]", newExe, currentExe, ec);
        return ec;
    }

    // This will use TEMP but this is not an issue since the executable will not be used anymore
    fs::path tempDir = fs::temp_directory_path(ec);
    fs::path tempBackup = tempDir / backupPath.filename();

    if (!MoveFileExW(backupPath.c_str(), tempBackup.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed MoveFileExW (input: {}, output: {}) [{}]", newExe, currentExe, ec);
        ec.clear();  // let the file inplace and register it for removal

        if (!MoveFileExW(backupPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        {
            Log::Error(L"Failed to schedule backup for deletion (path: {}) [{}]", backupPath, LastWin32Error());
        }
    }
    else
    {
        if (!MoveFileExW(tempBackup.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        {
            Log::Error(L"Failed to schedule temp backup for deletion (path: {}) [{}]", tempBackup, LastWin32Error());
        }
    }

    return {};
}
