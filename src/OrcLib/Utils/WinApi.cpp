//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include <windows.h>
#include <psapi.h>

#include "Utils/WinApi.h"
#include "Log/Log.h"

using namespace Orc;

namespace {

class UserTokenInformation
{
public:
    UserTokenInformation(std::vector<uint8_t>&& buffer)
        : m_buffer(std::move(buffer))
    {
        assert(m_buffer.size() >= sizeof(TOKEN_USER));
        if (m_buffer.size() < sizeof(TOKEN_USER))
        {
            m_buffer.resize(sizeof(TOKEN_USER));
        }
    }

    TOKEN_USER* Token() noexcept { return reinterpret_cast<TOKEN_USER*>(m_buffer.data()); }

    std::optional<PSID> Sid() noexcept
    {
        TOKEN_USER* t = Token();
        if (!t || !IsValidSid(t->User.Sid))
        {
            return {};
        }

        return t->User.Sid;
    }

private:
    std::vector<uint8_t> m_buffer;
};

class GroupTokenInformation
{
public:
    GroupTokenInformation(std::vector<uint8_t>&& buffer)
        : m_buffer(std::move(buffer))
    {
        assert(m_buffer.size() >= sizeof(TOKEN_PRIMARY_GROUP));
        if (m_buffer.size() < sizeof(TOKEN_PRIMARY_GROUP))
        {
            m_buffer.resize(sizeof(TOKEN_PRIMARY_GROUP));
        }
    }

    TOKEN_PRIMARY_GROUP* Token() noexcept { return reinterpret_cast<TOKEN_PRIMARY_GROUP*>(m_buffer.data()); }

    std::optional<PSID> Sid() noexcept
    {
        TOKEN_PRIMARY_GROUP* t = Token();
        if (!t || !IsValidSid(t->PrimaryGroup))
        {
            return {};
        }

        return t->PrimaryGroup;
    }

private:
    std::vector<uint8_t> m_buffer;
};

Result<GroupTokenInformation> GetEffectiveGroupTokenInformation();

struct LocalFreeDeleter
{
    void operator()(void* ptr) const
    {
        if (ptr)
            LocalFree(ptr);
    }
};

template <typename T>
using LocalPtr = std::unique_ptr<T, LocalFreeDeleter>;

Result<HANDLE> OpenEffectiveToken(DWORD desiredAccess)
{
    HANDLE hToken = nullptr;

    // Open the token with the same privileges as the process, not the thread (in case it's impersonating)
    BOOL OpenAsSelf = TRUE;
    if (OpenThreadToken(GetCurrentThread(), desiredAccess, OpenAsSelf, &hToken))
    {
        return hToken;
    }

    // No thread token means no impersonation
    DWORD err = GetLastError();
    Log::Debug("Failed OpenThreadToken [{}]", Win32Error(err));

    if (err != ERROR_NO_TOKEN)
    {
        return Win32Error(err);
    }

    if (!OpenProcessToken(GetCurrentProcess(), desiredAccess, &hToken))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed OpenProcessToken [{}]", ec);
        return ec;
    }

    return hToken;
}

Result<std::vector<uint8_t>> QueryEffectiveTokenBuffer(TOKEN_INFORMATION_CLASS tokenClass)
{
    auto hToken = OpenEffectiveToken(TOKEN_QUERY);
    if (!hToken)
    {
        Log::Debug("Failed to open effective token [{}]", hToken.error());
        return hToken.error();
    }

    Guard::FileHandle data(*hToken);

    DWORD dwLength = 0;
    if (!GetTokenInformation(data.value(), tokenClass, nullptr, 0, &dwLength))
    {
        auto err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER)
        {
            auto ec = Win32Error(err);
            Log::Debug("Failed GetTokenInformation sizing [{}]", ec);
            return ec;
        }
    }

    std::vector<uint8_t> buffer(dwLength);
    if (!GetTokenInformation(data.value(), tokenClass, buffer.data(), buffer.size(), &dwLength))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetTokenInformation [{}]", ec);
        return ec;
    }

    return buffer;
}

class SecurityDescriptorFactory
{
public:
    static SecurityDescriptorFactory& Instance();

    [[nodiscard]] Result<SECURITY_DESCRIPTOR*> GetSecurityDescriptor(DACL type);

private:
    SecurityDescriptorFactory() = default;

    Result<SECURITY_DESCRIPTOR*> CreateCurrentUserFullControlSD();
    Result<SECURITY_DESCRIPTOR*> CreateAdministratorsFullControlSD();
    Result<SECURITY_DESCRIPTOR*> CreateDefaultFullControlSD();

private:
    std::mutex m_mutex;
    LocalPtr<SECURITY_DESCRIPTOR> m_currentUserFullControlSD;
    LocalPtr<SECURITY_DESCRIPTOR> m_administratorsFullControlSD;
    LocalPtr<SECURITY_DESCRIPTOR> m_defaultFullControlSD;
};

SecurityDescriptorFactory& SecurityDescriptorFactory::Instance()
{
    static SecurityDescriptorFactory instance;
    return instance;
}

Result<SECURITY_DESCRIPTOR*> SecurityDescriptorFactory::GetSecurityDescriptor(DACL dacl)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    switch (dacl)
    {
        case DACL::OrcDefaultFullControl: {
            if (m_defaultFullControlSD)
            {
                return m_defaultFullControlSD.get();
            }

            if (auto sd = CreateDefaultFullControlSD())
            {
                m_defaultFullControlSD = LocalPtr<SECURITY_DESCRIPTOR>(sd.value());
                return m_defaultFullControlSD.get();
            }
            else
            {
                return sd.error();
            }
        }
        case DACL::AdministratorsFullControl: {
            if (m_administratorsFullControlSD)
            {
                return m_administratorsFullControlSD.get();
            }

            if (auto sd = CreateAdministratorsFullControlSD())
            {
                m_administratorsFullControlSD = LocalPtr<SECURITY_DESCRIPTOR>(sd.value());
                return m_administratorsFullControlSD.get();
            }
            else
            {
                return sd.error();
            }
        }
        case DACL::CurrentUserFullControl: {
            if (m_currentUserFullControlSD)
            {
                return m_currentUserFullControlSD.get();
            }

            if (auto sd = CreateCurrentUserFullControlSD())
            {
                m_currentUserFullControlSD = LocalPtr<SECURITY_DESCRIPTOR>(sd.value());
                return m_currentUserFullControlSD.get();
            }
            else
            {
                return sd.error();
            }
        }

        default:
            return std::make_error_code(std::errc::invalid_argument);
    }
}

Result<SECURITY_DESCRIPTOR*> CreateSecurityDescriptor(const std::wstring& sddl)
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed ConvertStringSecurityDescriptorToSecurityDescriptorW [{}]", ec);
        return ec;
    }

    if (!IsValidSecurityDescriptor(sd))
    {
        Log::Debug(L"Failed IsValidSecurityDescriptor");
        return std::errc::invalid_argument;
    }

    return static_cast<SECURITY_DESCRIPTOR*>(sd);
}

Result<UserTokenInformation> GetEffectiveUserTokenInformation()
{
    auto buffer = QueryEffectiveTokenBuffer(TokenUser);
    if (!buffer)
    {
        return buffer.error();
    }

    if (buffer->size() < sizeof(TOKEN_USER))
    {
        Log::Debug("GetTokenInformation returned undersized buffer");
        return std::errc::no_buffer_space;
    }

    UserTokenInformation info(std::move(*buffer));
    if (!info.Sid())
    {
        Log::Debug("Token contains invalid SID");
    }

    return info;
}

Result<GroupTokenInformation> GetEffectiveGroupTokenInformation()
{
    auto buffer = QueryEffectiveTokenBuffer(TokenPrimaryGroup);
    if (!buffer)
    {
        return buffer.error();
    }

    if (buffer->size() < sizeof(TOKEN_PRIMARY_GROUP))
    {
        Log::Debug("GetTokenInformation returned undersized buffer for primary group");
        return std::errc::no_buffer_space;
    }

    GroupTokenInformation info(std::move(*buffer));
    if (!info.Sid())
    {
        Log::Debug("Primary group token contains invalid SID");
    }

    return info;
}

Result<std::wstring> GetEffectiveUserSidString()
{
    auto tokenInfo = GetEffectiveUserTokenInformation();
    if (!tokenInfo)
    {
        Log::Debug("Failed to get effective user token information [{}]", tokenInfo.error());
        return tokenInfo.error();
    }

    if (!tokenInfo->Sid())
    {
        Log::Debug("Effective token does not contain a valid SID");
        return std::errc::bad_message;
    }

    LPWSTR szSID = nullptr;
    if (!ConvertSidToStringSidW(*tokenInfo->Sid(), &szSID))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed ConvertSidToStringSidW [{}]", ec);
        return ec;
    }

    LocalPtr<wchar_t> sidPtr(szSID);
    return {sidPtr.get()};
}

Result<SECURITY_DESCRIPTOR*> SecurityDescriptorFactory::CreateCurrentUserFullControlSD()
{
    auto sid = GetEffectiveUserSidString();
    if (!sid)
    {
        Log::Debug("Failed to get effective user SID string [{}]", sid.error());
        return sid.error();
    }

    const auto sddl = fmt::format(L"D:P(A;OICI;FA;;;{})", *sid);
    return CreateSecurityDescriptor(sddl);
}

Result<SECURITY_DESCRIPTOR*> SecurityDescriptorFactory::CreateAdministratorsFullControlSD()
{
    return CreateSecurityDescriptor(L"D:P(A;OICI;FA;;;BA)");
}

Result<SECURITY_DESCRIPTOR*> SecurityDescriptorFactory::CreateDefaultFullControlSD()
{
    auto sid = GetEffectiveUserSidString();
    if (!sid)
    {
        Log::Debug("Failed to get effective user SID string [{}]", sid.error());
        return sid.error();
    }

    // Try progressively simpler DACLs
    const std::array<std::wstring, 3> candidates = {
        fmt::format(L"D:P(A;OICI;FA;;;{})(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", *sid),  // user + admins + system
        fmt::format(L"D:P(A;OICI;FA;;;{})(A;OICI;FA;;;BA)", *sid),  // user + admins
        fmt::format(L"D:P(A;OICI;FA;;;{})", *sid),  // user only
    };

    for (const auto& sddl : candidates)
    {
        auto sd = CreateSecurityDescriptor(sddl);
        if (sd)
        {
            Log::Debug(L"Created default SD (SDDL: {})", sddl);
            return sd;
        }

        Log::Debug(L"Failed to create SD (SDDL: {}) [{}]", sddl, sd.error());
    }

    Log::Debug("All default SD candidates failed");
    return std::make_error_code(std::errc::operation_not_permitted);
}

inline bool HasWriteAccess(DWORD dwDesiredAccess)
{
    constexpr DWORD kWriteMask =
        GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
    return (dwDesiredAccess & kWriteMask) != 0;
}

inline bool HasExplicitSecurityDescriptor(LPSECURITY_ATTRIBUTES lpSecurityAttributes) noexcept
{
    return lpSecurityAttributes != nullptr && lpSecurityAttributes->lpSecurityDescriptor != nullptr;
}

Result<PACL> ExtractDacl(LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    PACL pDacl = nullptr;
    BOOL bDaclPresent = FALSE;
    BOOL bDaclDefaulted = FALSE;

    if (!GetSecurityDescriptorDacl(lpSecurityAttributes->lpSecurityDescriptor, &bDaclPresent, &pDacl, &bDaclDefaulted))
    {
        Log::Debug(L"Failed GetSecurityDescriptorDacl [{}]", LastWin32Error());
        return LastWin32Error();
    }

    if (!bDaclPresent)
    {
        return Win32Error(ERROR_INVALID_SECURITY_DESCR);
    }

    if (!IsValidAcl(pDacl))
    {
        Log::Debug(L"Failed IsValidAcl");
        return std::errc::invalid_argument;
    }

    return pDacl;
}

[[nodiscard]] Result<void>
UpdateSecurityInfo(HANDLE hFile, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PSID psidOwner, PSID psidGroup) noexcept
{
    auto dacl = ExtractDacl(lpSecurityAttributes);
    if (!dacl)
    {
        Log::Debug(L"Failed to extract DACL from security attributes [{}]", dacl.error());
        return dacl.error();
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
        *dacl,
        /*pSacl=*/nullptr);

    if (err != ERROR_SUCCESS)
    {
        return Win32Error(err);
    }

    return Orc::Success<void>();
}

[[nodiscard]] std::error_code RemoveFileIfExists(const std::wstring& path) noexcept
{
    if (DeleteFileW(path.c_str()))
    {
        return {};
    }

    auto ec = LastWin32Error();
    if (ec.value() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
    {
        return {};
    }

    Log::Debug(L"Failed DeleteFileW to handle CREATE_ALWAYS semantics (path: {}) [{}]", path, ec);
    std::filesystem::path filePath(path);
    std::wstring newPath = filePath.parent_path()
        / (filePath.stem().wstring() + L"_" + std::to_wstring(GetCurrentProcessId()) + filePath.extension().wstring());
    if (MoveFileExW(path.c_str(), newPath.c_str(), 0))
    {
        if (!MoveFileExW(newPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        {
            Log::Warn(L"Failed to schedule file for deletion on reboot (path: {}) [{}]", newPath, LastWin32Error());
        }

        return {};
    }

    Log::Debug(
        L"Failed MoveFileExW to handle CREATE_ALWAYS semantics (source: {}, destination: {}) [{}]", path, newPath, ec);
    return ec;
}

}  // namespace

namespace Orc {

std::wstring GetFullPathNameApi(const std::wstring& path, std::error_code& ec) noexcept
{
    try
    {
        DWORD dwLenRequired = GetFullPathNameW(path.c_str(), 0L, NULL, NULL);
        if (dwLenRequired == 0L)
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed GetFullPathNameW buffer length request (path: {}) [{}]", path, ec);
            return {};
        }

        std::wstring fullPath;
        fullPath.resize(dwLenRequired);

        dwLenRequired = GetFullPathNameW(path.c_str(), fullPath.size(), fullPath.data(), NULL);
        if (dwLenRequired == 0)
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed GetFullPathNameW buffer length request (path: {}) [{}]", path, ec);
            return {};
        }

        fullPath.resize(dwLenRequired);
        return fullPath;
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetFullPathNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetFullPathNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }
}

[[nodiscard]] Result<SECURITY_DESCRIPTOR*> GetSecurityDescriptor(DACL dacl) noexcept
{
    auto& factory = SecurityDescriptorFactory::Instance();
    return factory.GetSecurityDescriptor(dacl);
}

Result<Guard::FileHandle> CreateFileSafeApi(
    const wchar_t* lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwSharedMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplate,
    DACL dacl) noexcept
{
    SECURITY_ATTRIBUTES sa;
    SecureZeroMemory(&sa, sizeof(sa));

    auto sd = GetSecurityDescriptor(dacl);
    if (!sd)
    {
        return LastWin32Error();
    }

    sa.nLength = sizeof(sa);
    if (lpSecurityAttributes)
    {
        sa.bInheritHandle = lpSecurityAttributes->bInheritHandle;
    }
    else
    {
        sa.bInheritHandle = FALSE;
    }

    sa.lpSecurityDescriptor = *sd;

    // Handle CREATE_ALWAYS as CREATE_NEW to make sure opening the file with the correct ACL will be atomic.
    // This avoid a race condition where another user create the file and get an handle before ACLs are updated.
    if (dwCreationDisposition == CREATE_ALWAYS)
    {
        auto ec = RemoveFileIfExists(lpFileName);
        if (ec)
        {
            Log::Error(
                L"Failed to remove existing file to handle CREATE_ALWAYS semantics (path: {}) [{}]", lpFileName, ec);
            return ec;
        }

        dwCreationDisposition = CREATE_NEW;
    }

    Guard::FileHandle hFile = CreateFileW(
        lpFileName,
        dwDesiredAccess | WRITE_DAC | WRITE_OWNER,
        dwSharedMode,
        &sa,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplate);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        return LastWin32Error();
    }

    auto userInfo = GetEffectiveUserTokenInformation();
    if (!userInfo)
    {
        Log::Debug("Failed to get effective user token information [{}]", userInfo.error());
        return userInfo.error();
    }

    if (!userInfo->Sid())
    {
        Log::Debug("Effective token does not contain a valid SID");
        return std::errc::invalid_argument;
    }

    auto groupInfo = GetEffectiveGroupTokenInformation();
    if (!groupInfo)
    {
        Log::Debug("Failed to get effective group token information [{}]", groupInfo.error());
        return groupInfo.error();
    }

    if (!groupInfo->Sid())
    {
        Log::Debug("Effective token does not contain a valid group SID");
        return std::errc::invalid_argument;
    }

    auto rv = UpdateSecurityInfo(hFile.value(), &sa, *userInfo->Sid(), *groupInfo->Sid());
    if (!rv)
    {
        Log::Debug("Failed to update security information on file handle [{}]", rv.error());
        return rv.error();
    }

    return hFile;
}

//
// Wrapper over CreateFileW that set file ACL and owner on write access when lpSecurityAttributes is not set.
//
// This is required to work around the fact that when creating a file with write access and no security attributes, the
// file is created with a default ACL that are too permissive.
//
Result<Guard::FileHandle> CreateFileApi(
    const wchar_t* lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwSharedMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplate,
    BOOL ReplaceNullSecurityAttributes) noexcept
{
    const bool needsHardening = HasWriteAccess(dwDesiredAccess) && !HasExplicitSecurityDescriptor(lpSecurityAttributes)
        && ReplaceNullSecurityAttributes;

    if (!needsHardening)
    {
        Guard::FileHandle hFile = CreateFileW(
            lpFileName,
            dwDesiredAccess,
            dwSharedMode,
            lpSecurityAttributes,
            dwCreationDisposition,
            dwFlagsAndAttributes,
            hTemplate);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            return LastWin32Error();
        }

        return hFile;
    }

    return CreateFileSafeApi(
        lpFileName,
        dwDesiredAccess,
        dwSharedMode,
        lpSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplate,
        DACL::OrcDefaultFullControl);
}

std::wstring ExpandEnvironmentStringsApi(const wchar_t* szEnvString, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));
    const DWORD cchRequired = ::ExpandEnvironmentStringsW(szEnvString, NULL, 0L);
    if (cchRequired > cchMaxOutput)
    {
        ec.assign(ERROR_INSUFFICIENT_BUFFER, std::system_category());
        return {};
    }

    std::wstring output;
    try
    {
        output.resize(cchRequired);

        const DWORD cchDone = ::ExpandEnvironmentStringsW(szEnvString, output.data(), cchRequired);
        if (cchDone == 0)
        {
            ec.assign(GetLastError(), std::system_category());
            Log::Debug(L"Failed ExpandEnvironmentStringsApi '{}' [{}]", szEnvString, ec);
            return {};
        }

        if (cchDone != cchRequired)
        {
            ec.assign(ERROR_INVALID_DATA, std::system_category());
            Log::Debug(L"Failed ExpandEnvironmentStringsApi '{}'", szEnvString);
            return {};
        }

        // remove terminating NULL character
        output.resize(cchDone - 1);
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed ExpandEnvironmentStringsApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "ExpandEnvironmentStringsApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return output;
}

std::wstring ExpandEnvironmentStringsApi(const wchar_t* szEnvString, std::error_code& ec) noexcept
{
    return ExpandEnvironmentStringsApi(szEnvString, ORC_MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring GetWorkingDirectoryApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring directory;
    try
    {
        directory.resize(cbMaxOutput);

        size_t cch = ::GetCurrentDirectoryW(cchMaxOutput, directory.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetWorkingDirectoryApi [{}]", ec);
            return {};
        }

        directory.resize(cch);
        directory.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetWorkingDirectoryApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetWorkingDirectoryApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return directory;
}

std::wstring GetWorkingDirectoryApi(std::error_code& ec) noexcept
{
    return GetWorkingDirectoryApi(ORC_MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring GetTempPathApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring path;
    try
    {
        path.resize(cchMaxOutput);

        size_t cch = ::GetTempPathW(cchMaxOutput, path.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetTempPathApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetTempPathApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetTempPathApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetTempPathApi(std::error_code& ec) noexcept
{
    return GetTempPathApi(ORC_MAX_PATH * sizeof(wchar_t), ec);
}

std::wstring
GetTempFileNameApi(const wchar_t* lpPathName, const wchar_t* lpPrefixString, UINT uUnique, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = ORC_MAX_PATH;
    const DWORD cbMaxOutput = cchMaxOutput * sizeof(wchar_t);

    std::wstring path;
    try
    {
        path.resize(cbMaxOutput);

        size_t cch = ::GetTempFileNameW(lpPathName, lpPrefixString, uUnique, path.data());
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetTempFileNameApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetTempFileNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetTempFileNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetTempFileNameApi(const wchar_t* lpPathName, std::error_code& ec) noexcept
{
    return GetTempFileNameApi(lpPathName, nullptr, 0, ec);
}

std::wstring GetModuleFileNameApi(HMODULE hModule, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    std::wstring path;

    try
    {
        path.resize(cbMaxOutput);

        DWORD cch = ::GetModuleFileNameW(hModule, path.data(), path.size());

        DWORD lastError = GetLastError();
        if (lastError != ERROR_SUCCESS)
        {
            ec.assign(lastError, std::system_category());
            Log::Debug("Failed GetModuleFileNameApi [{}]", ec);
            return {};
        }

        if (cch == path.size())
        {
            // Assume it is an error for compatibility with XP, see MSDN remark:
            //
            // Windows XP: If the buffer is too small to hold the module name, the function returns nSize. The last
            // error code remains ERROR_SUCCESS. If nSize is zero, the return value is zero and the last error code is
            // ERROR_SUCCESS.
            //
            ec.assign(ERROR_INSUFFICIENT_BUFFER, std::system_category());
            Log::Debug("Failed GetModuleFileNameApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetModuleFileNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetTempFileNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetModuleFileNameApi(HMODULE hModule, std::error_code& ec) noexcept
{
    return GetModuleFileNameApi(hModule, 32767 * sizeof(wchar_t), ec);
}

std::wstring GetModuleFileNameExApi(HANDLE hProcess, HMODULE hModule, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    std::wstring path;

    try
    {
        path.resize(cbMaxOutput);

        DWORD cch = ::GetModuleFileNameExW(hProcess, hModule, path.data(), path.size());

        DWORD lastError = GetLastError();
        if (cch == 0 || lastError != ERROR_SUCCESS)
        {
            ec.assign(lastError, std::system_category());
            Log::Debug("Failed GetModuleFileNameExApi [{}]", ec);
            return {};
        }

        path.resize(cch);
        path.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetModuleFileNameExApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetModuleFileNameExApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return path;
}

std::wstring GetModuleFileNameExApi(HANDLE hProcess, HMODULE hModule, std::error_code& ec) noexcept
{
    return GetModuleFileNameExApi(hProcess, hModule, 32767 * sizeof(wchar_t), ec);
}

std::wstring GetComputerNameApi(size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring result;
    try
    {
        result.resize(cbMaxOutput);

        DWORD nSize = cchMaxOutput;
        size_t cch = ::GetComputerNameW(result.data(), &nSize);
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetComputerNameW [{}]", ec);
            return {};
        }

        result.resize(cch);
        result.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetComputerNameApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetComputerNameApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return result;
}

std::wstring GetComputerNameApi(std::error_code& ec) noexcept
{
    return GetComputerNameApi(256 * sizeof(wchar_t), ec);
}

std::wstring GetComputerNameExApi(ComputerNameFormat format, size_t cbMaxOutput, std::error_code& ec) noexcept
{
    const DWORD cchMaxOutput = static_cast<DWORD>(cbMaxOutput / sizeof(wchar_t));

    std::wstring result;
    try
    {
        result.resize(cbMaxOutput);

        DWORD nSize = cchMaxOutput;
        size_t cch = ::GetComputerNameW(result.data(), &nSize);
        if (cch == 0)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed GetComputerNameW [{}]", ec);
            return {};
        }

        result.resize(cch);
        result.shrink_to_fit();
    }
    catch (const std::length_error& e)
    {
        Log::Debug("Failed GetComputerNameExApi [exception: {}]", e.what());
        ec = std::make_error_code(std::errc::not_enough_memory);
        return {};
    }
    catch (...)
    {
        std::cerr << "GetComputerNameExApi had unexpected recoverable exception" << std::endl;
        ec = std::make_error_code(std::errc::resource_unavailable_try_again);
        return {};
    }

    return result;
}

std::wstring GetComputerNameExApi(ComputerNameFormat format, std::error_code& ec) noexcept
{
    return GetComputerNameExApi(format, 256 * sizeof(wchar_t), ec);
}

}  // namespace Orc
