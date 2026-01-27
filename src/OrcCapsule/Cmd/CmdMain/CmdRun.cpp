#include "CmdRun.h"

#include <array>
#include <iostream>

#include <aclapi.h>  // advapi32
#include <sddl.h>

#include "Utilities/Log.h"
#include "Utilities/Error.h"

#include "Utilities/File.h"
#include "Utilities/Process.h"
#include "Utilities/Resource.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

#include "Cmd/Architecture.h"
#include "Cmd/CmdMain/Runner.h"

#include "OrcCapsuleCLI.h"

constexpr std::wstring_view kWorkingTempDirName(L"WorkingTemp");

constexpr std::wstring_view kResTypeBinary = L"BINARY";
constexpr std::wstring_view kWolfLauncherConfig = L"WOLFLAUNCHER_CONFIG";

namespace {

[[nodiscard]] constexpr std::wstring_view TrimLeadingSpaceView(std::wstring_view sv) noexcept
{
    const std::wstring_view whitespace = L" \t";

    const auto first_content = sv.find_first_not_of(whitespace);
    if (first_content == std::wstring_view::npos)
    {
        return {};
    }

    return sv.substr(first_content);
}

struct AclDeleter
{
    void operator()(PACL p) const
    {
        if (p)
        {
            LocalFree(p);
        }
    }
};

struct SecurityDescriptorDeleter
{
    void operator()(PSECURITY_DESCRIPTOR p) const
    {
        if (p)
        {
            LocalFree(p);
        }
    }
};

[[nodiscard]] std::error_code OpenEffectiveToken(DWORD desiredAccess, HANDLE& hToken)
{
    hToken = INVALID_HANDLE_VALUE;

    // Open the token with the same privileges as the process, not the thread (in case it's impersonating)
    BOOL OpenAsSelf = TRUE;
    if (OpenThreadToken(GetCurrentThread(), desiredAccess, OpenAsSelf, &hToken))
    {
        return {};
    }

    // No thread token means no impersonation
    auto ec = LastWin32Error();
    Log::Debug("Failed OpenThreadToken [{}]", ec);
    if (ec)
    {
        if (ec.value() != ERROR_NO_TOKEN)
        {
            return ec;
        }

        ec.clear();
    }

    if (!OpenProcessToken(GetCurrentProcess(), desiredAccess, &hToken))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed OpenProcessToken [{}]", ec);
        return ec;
    }

    return {};
}

[[nodiscard]] std::error_code
QueryEffectiveTokenBuffer(TOKEN_INFORMATION_CLASS tokenClass, std::vector<uint8_t>& buffer)
{
    HANDLE hToken;
    auto ec = OpenEffectiveToken(TOKEN_QUERY, hToken);
    if (ec)
    {
        Log::Debug("Failed to open effective token [{}]", ec);
        return ec;
    }

    ScopedFileHandle data(hToken);

    DWORD dwLength = 0;
    if (!GetTokenInformation(data.get(), tokenClass, nullptr, 0, &dwLength))
    {
        auto ec = LastWin32Error();
        if (ec.value() != ERROR_INSUFFICIENT_BUFFER)
        {
            Log::Debug("Failed GetTokenInformation sizing [{}]", ec);
            return ec;
        }

        ec.clear();
    }

    buffer.resize(dwLength);
    if (!GetTokenInformation(data.get(), tokenClass, buffer.data(), buffer.size(), &dwLength))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetTokenInformation [{}]", ec);
        return ec;
    }

    return {};
}

struct UserTokenInformation
{
    std::vector<uint8_t> buffer;

    // items belows points into buffer
    TOKEN_USER* token = nullptr;
    std::optional<PSID> sid;
};

[[nodiscard]] std::error_code GetEffectiveUserTokenInformation(UserTokenInformation& tokenInfo)
{
    std::vector<uint8_t> buffer;
    auto ec = QueryEffectiveTokenBuffer(TokenUser, buffer);
    if (ec)
    {
        return ec;
    }

    if (buffer.size() < sizeof(TOKEN_USER))
    {
        Log::Debug("GetTokenInformation returned undersized buffer");
        return std::make_error_code(std::errc::no_buffer_space);
    }

    tokenInfo.buffer = std::move(buffer);
    tokenInfo.token = reinterpret_cast<TOKEN_USER*>(tokenInfo.buffer.data());

    if (!IsValidSid(tokenInfo.token->User.Sid))
    {
        Log::Debug("Token contains invalid SID");
    }
    else
    {
        tokenInfo.sid = tokenInfo.token->User.Sid;
    }

    return {};
}

struct GroupTokenInformation
{
    std::vector<uint8_t> buffer;

    // items belows points into buffer
    TOKEN_PRIMARY_GROUP* token = nullptr;
    std::optional<PSID> sid;
};

[[nodiscard]] std::error_code GetEffectiveGroupTokenInformation(GroupTokenInformation& tokenInfo)
{
    std::vector<uint8_t> buffer;
    auto ec = QueryEffectiveTokenBuffer(TokenPrimaryGroup, buffer);
    if (ec)
    {
        return ec;
    }

    if (buffer.size() < sizeof(TOKEN_PRIMARY_GROUP))
    {
        Log::Debug("GetTokenInformation returned undersized buffer for primary group");
        return std::make_error_code(std::errc::no_buffer_space);
    }

    tokenInfo.buffer = std::move(buffer);
    tokenInfo.token = reinterpret_cast<TOKEN_PRIMARY_GROUP*>(tokenInfo.buffer.data());

    if (!IsValidSid(tokenInfo.token->PrimaryGroup))
    {
        Log::Debug("Primary group token contains invalid SID");
    }
    else
    {
        tokenInfo.sid = tokenInfo.token->PrimaryGroup;
    }

    return {};
}

[[nodiscard]] std::error_code GetEffectiveUserSidString(std::wstring& sid)
{
    UserTokenInformation tokenInfo;
    auto ec = GetEffectiveUserTokenInformation(tokenInfo);
    if (ec)
    {
        Log::Debug("Failed to get effective user token information [{}]", ec);
        return ec;
    }

    if (!tokenInfo.sid)
    {
        Log::Debug("Effective token does not contain a valid SID");
        return std::make_error_code(std::errc::bad_message);
    }

    LPWSTR szSID = nullptr;
    if (!ConvertSidToStringSidW(*tokenInfo.sid, &szSID))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed ConvertSidToStringSidW [{}]", ec);
        return ec;
    }

    LocalPtr<wchar_t> sidPtr(szSID);
    sid = {sidPtr.get()};
    return {};
}

[[nodiscard]] std::error_code CreateDefaultFullControlSD(PSECURITY_DESCRIPTOR* sd)
{
    std::wstring sid;
    auto ec = GetEffectiveUserSidString(sid);
    if (ec)
    {
        Log::Debug("Failed to get effective user SID string [{}]", ec);
        return ec;
    }

    // Try progressively simpler DACLs — some may fail depending on user privileges
    const std::array<std::wstring, 3> candidates = {
        fmt::format(L"D:P(A;OICI;FA;;;{})(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", sid),  // user + admins + system
        fmt::format(L"D:P(A;OICI;FA;;;{})(A;OICI;FA;;;BA)", sid),  // user + admins
        fmt::format(L"D:P(A;OICI;FA;;;{})", sid),  // user only
    };

    for (const auto& sddl : candidates)
    {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, sd, nullptr))
        {
            Log::Debug(L"Failed to create SD (SDDL: {}) [{}]", sddl, LastWin32Error());
            continue;
        }

        if (!IsValidSecurityDescriptor(*sd))
        {
            LocalFree(*sd);
            Log::Debug(L"Created SD is invalid (SDDL: {})", sddl);
            continue;
        }

        Log::Debug(L"Created default SD (SDDL: {})", sddl);
        return {};
    }

    Log::Debug("All default SD candidates failed");
    return std::make_error_code(std::errc::operation_not_permitted);
}

[[nodiscard]] std::error_code GetTempPathApi(size_t cbMaxOutput, std::wstring& temp) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring path;
    try
    {
        path.resize(cchMaxOutput);

        size_t cch = ::GetTempPathW(cchMaxOutput, path.data());
        if (cch == 0)
        {
            auto ec = LastWin32Error();
            Log::Debug("Failed GetTempPathW [{}]", ec);
            return ec;
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetTempPathApi [exception: {}]", e.what());
        return std::make_error_code(std::errc::not_enough_memory);
    }
    catch (...)
    {
        std::cerr << "GetTempPathApi had unexpected recoverable exception" << std::endl;
        return std::make_error_code(std::errc::resource_unavailable_try_again);
    }

    temp = std::move(path);
    return {};
}

[[nodiscard]] std::error_code GetTemporaryDirectory(std::wstring& path)
{
    std::wstring temp;
    DWORD cchMaxTemp = MAX_PATH;
    temp.resize(cchMaxTemp);
    DWORD cchTemp = GetEnvironmentVariableW(L"TEMP", temp.data(), cchMaxTemp);
    if (cchTemp != 0)
    {
        temp.resize(cchTemp);
        path = std::move(temp);
        return {};
    }

    Log::Debug("Failed GetEnvironmentVariableW on 'TEMP' variable [{}]", ::GetLastError());

    // Fallback that could select \Windows\Temp which is not ideal because of permission issues
    auto ec = GetTempPathApi(MAX_PATH, path);
    if (ec)
    {
        Log::Debug("Failed GetTempPathApi [{}]", ec);
        return ec;
    }

    return {};
}

[[nodiscard]] std::error_code RemoveAll(const std::wstring& path)
{
    std::error_code ec;

    bool exists = std::filesystem::exists(path, ec);
    if (ec)
    {
        Log::Debug("Failed 'exists' [{}]", ec);
        return ec;
    }

    if (!exists)
    {
        return {};
    }

    std::filesystem::remove_all(path, ec);
    if (ec)
    {
        Log::Debug("Failed 'remove_all' [{}]", ec);
        return ec;
    }

    return {};
}

[[nodiscard]] std::error_code CreateDirectoryWithACL(const std::wstring& path)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    auto ec = CreateDefaultFullControlSD(&sd);
    if (ec)
    {
        Log::Debug("Failed to create default security descriptor [{}]", ec);
        return ec;
    }

    SECURITY_ATTRIBUTES sa;
    SecureZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    if (!CreateDirectoryW(path.c_str(), &sa))
    {
        // Will also fail if already exists
        auto ec = LastWin32Error();
        Log::Debug(L"Failed CreateDirectoryW [{}]", ec);
        return ec;
    }

    return {};
}

[[nodiscard]] std::error_code ExtractDacl(LPSECURITY_ATTRIBUTES lpSecurityAttributes, PACL& pDacl)
{
    BOOL bDaclPresent = FALSE;
    BOOL bDaclDefaulted = FALSE;

    if (!GetSecurityDescriptorDacl(lpSecurityAttributes->lpSecurityDescriptor, &bDaclPresent, &pDacl, &bDaclDefaulted))
    {
        Log::Debug(L"Failed GetSecurityDescriptorDacl [{}]", LastWin32Error());
        return LastWin32Error();
    }

    if (!bDaclPresent)
    {
        return std::error_code(ERROR_INVALID_SECURITY_DESCR, std::system_category());
    }

    if (!IsValidAcl(pDacl))
    {
        Log::Debug(L"Failed IsValidAcl");
        return std::make_error_code(std::errc::invalid_argument);
    }

    return {};
}

[[nodiscard]] std::error_code
UpdateSecurityInfo(HANDLE hFile, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PSID psidOwner, PSID psidGroup) noexcept
{
    PACL dacl = nullptr;
    auto ec = ExtractDacl(lpSecurityAttributes, dacl);
    if (ec)
    {
        Log::Debug(L"Failed to extract DACL from security attributes [{}]", ec);
        return ec;
    }

    SECURITY_INFORMATION securityInformation = DACL_SECURITY_INFORMATION;

    if (psidOwner)
    {
        securityInformation |= OWNER_SECURITY_INFORMATION;
    }

    if (psidGroup)
    {
        securityInformation |= GROUP_SECURITY_INFORMATION;
    }

    DWORD err = SetSecurityInfo(
        hFile,
        SE_FILE_OBJECT,
        securityInformation | PROTECTED_DACL_SECURITY_INFORMATION,
        psidOwner,
        psidGroup,
        dacl,
        /*pSacl=*/nullptr);

    if (err != ERROR_SUCCESS)
    {
        return std::error_code(err, std::system_category());
    }

    return {};
}

[[nodiscard]] std::error_code SetEffectiveUserSecurityInfo(
    const std::wstring& path,
    SECURITY_ATTRIBUTES& sa,
    bool useTakeOwnerShipPrivilege = false) noexcept
{
    ScopedFileHandle hFile(CreateFileW(
        path.c_str(),
        READ_CONTROL | WRITE_DAC | WRITE_OWNER,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL));

    if (hFile.get() == INVALID_HANDLE_VALUE)
    {
        return LastWin32Error();
    }

    UserTokenInformation userTokenInfo;
    auto ec = GetEffectiveUserTokenInformation(userTokenInfo);
    if (ec)
    {
        Log::Debug("Failed to get effective user token information [{}]", ec);
        return ec;
    }

    if (!userTokenInfo.sid)
    {
        Log::Debug("Effective user token information does not contain a valid SID");
        return std::make_error_code(std::errc::bad_message);
    }

    GroupTokenInformation groupTokenInfo;
    ec = GetEffectiveGroupTokenInformation(groupTokenInfo);
    if (ec)
    {
        Log::Debug("Failed to get effective group token information [{}]", ec);
        return ec;
    }

    if (!groupTokenInfo.sid)
    {
        Log::Debug("Effective group token information does not contain a valid SID");
        return std::make_error_code(std::errc::bad_message);
    }

    ec = UpdateSecurityInfo(hFile.get(), &sa, *userTokenInfo.sid, *groupTokenInfo.sid);
    if (ec)
    {
        Log::Debug(L"Failed to update security info [{}]", ec);
        return ec;
    }

    return {};
}

std::error_code CreateWorkingTemp(std::optional<std::wstring> basedir, std::wstring& safeTemp)
{
    if (!basedir || basedir->empty())
    {
        std::wstring systemTemp;
        auto ec = GetTemporaryDirectory(systemTemp);
        if (ec)
        {
            return ec;
        }

        basedir = std::move(systemTemp);
    }

    PSECURITY_DESCRIPTOR sd = nullptr;
    auto ec = CreateDefaultFullControlSD(&sd);
    if (ec)
    {
        Log::Debug("Failed to create default security descriptor [{}]", ec);
        return ec;
    }

    SECURITY_ATTRIBUTES sa;
    SecureZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    //
    // While there is a specific SECURITY_ATTRIBUTE, this not secure as it will fail if the directory already exists.
    // The call to SetEffectiveUserSecurityInfo below is not atomic either, one could have open a handle before.
    //
    // As the directory cannot be removed and created atomically since multiple simultaneous runs are now supported, the
    // function CreateRestrictedDirectory is responsible of creating the final sub-directory with correct ACLs
    // atomically.
    //
    // The purpose of CreateDirectoryW/SetEffectiveUserSecurityInfo here is to ensure any older temporary directory with
    // permissive ACLs that would have been created by previous version is fixed to have correct ACLs.
    //
    auto temp = fmt::format(L"{}\\{}", *basedir, kWorkingTempDirName);
    if (!CreateDirectoryW(temp.c_str(), &sa))
    {
        auto ec = LastWin32Error();
        if (ec.value() == ERROR_ALREADY_EXISTS)
        {
            ec = SetEffectiveUserSecurityInfo(temp.c_str(), sa);
            if (ec)
            {
                Log::Debug(
                    L"Failed to set effective user security info on working temp directory (path: {}) [{}]", temp, ec);
                ec.clear();  // what is critical is that the final restricted sub-directory has correct ACLs
            }
        }
        else
        {
            Log::Debug(L"Failed CreateDirectoryW (path: {}) [{}]", temp, ec);
            return ec;
        }
    }

    safeTemp = std::move(temp);
    return {};
}

[[nodiscard]] std::error_code CreateRestrictedDirectory(const std::wstring& parent, std::wstring& restrictedPath)
{
    const std::filesystem::path path = fmt::format(L"{}\\{}.tmp", parent, GetCurrentProcessId());

    auto ec = RemoveAll(path);
    if (ec)
    {
        Log::Debug(L"Failed to remove existing restricted temporary directory (path: {}) [{}]", path, ec);
        return ec;
    }

    ec = CreateDirectoryWithACL(path);
    if (ec)
    {
        Log::Debug(L"Failed to create restricted temporary directory (path: {}) [{}]", path, ec);
        return ec;
    }

    restrictedPath = std::move(path);
    return {};
}

[[nodiscard]] std::error_code SetTemporaryEnvironmentVariables(const std::wstring& temp)
{
    if (temp.empty())
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (!SetEnvironmentVariableW(L"TEMP", temp.c_str()))
    {
        auto ec = LastWin32Error();
        Log::Error(L"Failed to set %TEMP% to '{}' [{}]", temp, ec);
        return ec;
    }

    if (!SetEnvironmentVariableW(L"TMP", temp.c_str()))
    {
        auto ec = LastWin32Error();
        Log::Error(L"Failed to set %TMP% to '{}' [{}]", temp, ec);
        return ec;
    }

    return {};
}

[[nodiscard]] std::error_code
LaunchRunnerProcess(const fs::path& exePath, std::vector<std::wstring> args, PROCESS_INFORMATION& pi)
{
    std::error_code ec;

    bool implicitWolf = false;
    if (args.empty())
    {
        implicitWolf = true;
    }
    else
    {
        auto command = TrimLeadingSpaceView(args[0]);
        if (!command.empty() && (command[0] == L'/' || command[0] == L'-'))
        {
            implicitWolf = true;
        }
    }

    if (implicitWolf)
    {
        std::vector<uint8_t> config;
        ec = LoadResourceData(NULL, kWolfLauncherConfig.data(), kResTypeBinary.data(), config);
        if (!config.empty())
        {
            args.insert(args.begin(), L"WolfLauncher");
        }
    }

    // Inject Capsule Handle
    ScopedFileHandle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION, TRUE, GetCurrentProcessId()));
    if (!hProcess)
    {
        Log::Error(L"Failed OpenProcess on self [{}]", LastWin32Error());
        args.push_back(L"/capsule=<null>");
    }
    else
    {
        args.push_back(fmt::format(L"/capsule={:#x}", reinterpret_cast<uint64_t>(hProcess.get())));
    }

    bool hasNoWait = false;
    std::vector<std::wstring> childArgs;
    childArgs.reserve(args.size());

    for (auto& arg : args)
    {
        // --no-relocate is a capsule-only flag; child tools do not understand it.
        if (arg == L"--no-relocate" || arg == L"/norelocate")
        {
            continue;
        }

        if (arg == L"--no-wait" || arg == L"/nowait")
        {
            hasNoWait = true;
            continue;
        }

        if (arg == L"--force" || arg == L"-f")
        {
            arg = L"/force";
        }

        if (arg == L"--debug")
        {
            arg = L"/debug";
        }

        childArgs.push_back(arg);
    }

    std::wstring cmdLine;
    for (const auto& arg : childArgs)
    {
        cmdLine += L" " + arg;
    }

    DWORD dwCreationFlags = CREATE_UNICODE_ENVIRONMENT;
    std::wstring application;
    if (hasNoWait)
    {
        std::wstring cmdExe;
        cmdExe.resize(MAX_PATH);
        DWORD len = ExpandEnvironmentStringsW(L"%ComSpec%", cmdExe.data(), static_cast<DWORD>(cmdExe.size()));
        if (len == 0)
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed ExpandEnvironmentStringsW for %ComSpec% [{}]", ec);
            return ec;
        }

        application = cmdExe.substr(0, len - 1);  // remove null terminator
        cmdLine = fmt::format(L"\"{}\" /Q /E:OFF /D /C \"{} {}\"", application, exePath.native(), cmdLine);

        dwCreationFlags |= CREATE_NEW_PROCESS_GROUP;
        dwCreationFlags |= DETACHED_PROCESS;
    }
    else
    {
        application = exePath.native();
    }

    std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back(0);

    SecureZeroMemory(&pi, sizeof(pi));

    STARTUPINFOW si;
    SecureZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcessW(
            application.c_str(), cmdBuffer.data(), nullptr, nullptr, TRUE, dwCreationFlags, nullptr, nullptr, &si, &pi))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed CreateProcessW (path: {}) [{}]", exePath, ec);
        return ec;
    }

    return {};
}

}  // namespace

[[nodiscard]] std::error_code HandleRun(const CapsuleOptions& opts, DWORD& childExitCode)
{
    std::error_code ec;

    if (opts.tempDir.has_value())
    {
        Log::Debug(L"Temp directory: {}", opts.tempDir.value_or(L"<unset>"));
    }

    // Create a safe directory with restricted ACLs
    auto args = opts.rawArgs;
    fs::path safeTemp;
    {
        std::wstring workingTemp;

        if (opts.safeTemp)
        {
            safeTemp = *opts.safeTemp;
        }
        else
        {
            ec = CreateWorkingTemp(opts.tempDir, workingTemp);
            if (ec)
            {
                Log::Error(L"Failed to create working directory [{}]", ec);
                return ec;
            }

            std::wstring restrictedTemp;
            ec = CreateRestrictedDirectory(workingTemp, restrictedTemp);
            if (ec)
            {
                Log::Error(L"Failed to create restricted temporary directory [{}]", ec);
                return ec;
            }

            safeTemp = restrictedTemp;
        }

        Log::Debug(L"Safe directory: {}", safeTemp);

        // pass this internal parameter so the child can use it without testing ACLs but do not duplicate
        auto safeTempArg = fmt::format(L"/safetemp={}", safeTemp);
        if (std::find(std::cbegin(args), std::cend(args), safeTempArg) == std::cend(args))
        {
            args.push_back(safeTempArg);
        }
    }

    ec = SetTemporaryEnvironmentVariables(safeTemp);
    if (ec)
    {
        Log::Error(L"Failed to set temporary environment variables (path: {}) [{}]", safeTemp, ec);
        ec.clear();
    }

    if (!opts.relocate.has_value() || *opts.relocate)
    {
        ec = RelocateOrcFromNetwork(safeTemp, args);
        if (ec)
        {
            Log::Error(L"Failed to relocate executable [{}]", ec);
            ec.clear();  // continue on error as it is the best outcome
        }
    }

    fs::path runnerPath;
    ec = ExtractCompatibleRunner(safeTemp, true, runnerPath);
    if (ec)
    {
        Log::Error(L"Failed to extract runner (output directory: {}) [{}]", safeTemp, ec);
        return ec;
    }

    auto cleanup = MakeScopeGuard([path = runnerPath]() noexcept {
        auto err = RemoveFile(path);
        if (err)
        {
            Log::Error(L"Failed to schedule file for deletion (path: {}) [{}]", path, err);
            err.clear();
        }
    });

    Log::Debug(L"Execute '{}'", runnerPath);

    PROCESS_INFORMATION pi;
    ec = LaunchRunnerProcess(runnerPath, args, pi);
    if (ec)
    {
        Log::Error(L"Failed LaunchRunnerProcess (path: {}) [{}]", runnerPath, ec);
        return ec;
    }

    ec = WaitChildProcess(pi, runnerPath, childExitCode);
    if (ec)
    {
        Log::Error(L"Failed WaitChildProcess (path: {}) [{}]", runnerPath, ec);
        ec.clear();  // best outcome is to continue
    }

    return {};
}

void ParseExecuteCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::RunExplicit;

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

    // Parse remaining arguments as global options or child args
    while (idx < args.size())
    {
        if (TryParseGlobalOption(idx, args, opts))
        {
            continue;
        }

        opts.normalizedArgs.push_back(args[idx]);
        idx++;
    }
}

CommandDescriptor GetExecuteCommandDescriptor()
{
    return {
        CapsuleMode::RunExplicit,
        L"execute",
        L"   execute [x64|x64-xp|x86|x86-xp|arm64]\n"
        L"      Extract and run the embedded binary\n"
        L"      x64          Execute 64-bit binary\n"
        L"      x86          Execute 32-bit binary\n"
        L"      x64-xp       Execute Windows XP 64-bit binary\n"
        L"      x86-xp       Execute Windows XP 32-bit binary\n"
        L"      arm64        Execute Windows ARM 64-bit binary\n",
        ParseExecuteCommand,
        nullptr,
        nullptr,
    };
}
