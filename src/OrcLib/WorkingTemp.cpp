#include "WorkingTemp.h"

#include <windows.h>

#include "Utils/Guard.h"
#include "Utils/WinApi.h"
#include "Strings.h"

#include "Text/Fmt/Result.h"

using namespace Orc;

namespace {

std::wstring_view kWorkingTempDirName(L"WorkingTemp");

struct AclDeleter
{
    void operator()(PACL p) const
    {
        if (p)
            LocalFree(p);
    }
};

struct SecurityDescriptorDeleter
{
    void operator()(PSECURITY_DESCRIPTOR p) const
    {
        if (p)
            LocalFree(p);
    }
};

Result<std::wstring> GetTemporaryDirectory()
{
    std::wstring temp;
    size_t cchMaxTemp = ORC_MAX_PATH;
    temp.resize(cchMaxTemp);
    DWORD cchTemp = GetEnvironmentVariableW(L"TEMP", temp.data(), cchMaxTemp);
    if (cchTemp != 0)
    {
        temp.resize(cchTemp);
        return temp;
    }

    Log::Debug("Failed GetEnvironmentVariableW on 'TEMP' variable [{}]", LastWin32Error());

    // Fallback that could select \Windows\Temp which is not ideal because of permission issues
    std::error_code ec;
    temp = GetTempPathApi(ec);
    if (ec)
    {
        Log::Debug("Failed GetTempPathApi [{}]", ec);
        return ec;
    }

    return temp;
}

Result<void> RemoveAll(const std::wstring& path)
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
        return Orc::Success<void>();
    }

    std::filesystem::remove_all(path, ec);
    if (ec)
    {
        Log::Debug("Failed 'remove_all' [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

Result<bool> CheckDirectoryWithUserOnlyACL(const std::wstring& path, PSID pUserSid, DWORD dwAccess)
{
    DWORD attribute = GetFileAttributesW(path.c_str());
    if (attribute == INVALID_FILE_ATTRIBUTES)
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetFileAttributesW [{}]", ec);
        return ec;
    }

    if ((attribute & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)
    {
        Log::Debug(L"Path is not a directory (value: {})", path);
        return false;
    }

    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(
            path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd)
        != ERROR_SUCCESS)
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetNamedSecurityInfoW [{}]", ec);
        return ec;
    }

    std::unique_ptr<void, SecurityDescriptorDeleter> sdGuard(sd);

    // Check if the DACL has exactly one ACE granting full control to the current user
    ACL_SIZE_INFORMATION aclSizeInfo;
    if (!GetAclInformation(dacl, &aclSizeInfo, sizeof(aclSizeInfo), AclSizeInformation))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetAclInformation [{}]", ec);
        return ec;
    }

    if (aclSizeInfo.AceCount != 1)
    {
        Log::Debug("Path has multiple ACLs");
        return false;
    }

    ACL_REVISION_INFORMATION aclRevInfo;
    if (!GetAclInformation(dacl, &aclRevInfo, sizeof(aclRevInfo), AclRevisionInformation))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetAclInformation (revision) [{}]", ec);
        return ec;
    }

    ACCESS_ALLOWED_ACE* pAce;
    if (!GetAce(dacl, 0, reinterpret_cast<LPVOID*>(&pAce)))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetAce [{}]", ec);
        return ec;
    }

    if (pAce->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
    {
        Log::Debug("Path ACE is not of type ACCESS_ALLOWED_ACE_TYPE");
        return false;
    }

    PSID pAceSid = reinterpret_cast<PSID>(&pAce->SidStart);

    if (!EqualSid(pAceSid, pUserSid))
    {
        Log::Debug("Path ACL does not match user SID requirement");
        return false;
    }

    if (pAce->Mask != dwAccess)  // FILE_ALL_ACCESS
    {
        Log::Debug("Path ACL does not match access mask requirement");
        return false;
    }

    return true;
}

Result<void> CreateDirectoryWithUserOnlyACL(const std::wstring& path)
{
    Guard::Handle hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, hToken.data()))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed OpenProcessToken [{}]", ec);
        return ec;
    }

    // Get the user SID from the token
    DWORD dwLength = 0;
    GetTokenInformation(hToken.value(), TokenUser, NULL, 0, &dwLength);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetTokenInformation (sizing) [{}]", ec);
        return ec;
    }

    std::vector<BYTE> userTokenBuffer(dwLength);
    if (!GetTokenInformation(hToken.value(), TokenUser, userTokenBuffer.data(), dwLength, &dwLength))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetTokenInformation [{}]", ec);
        return ec;
    }

    PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(userTokenBuffer.data());
    PSID pUserSid = pTokenUser->User.Sid;

    // Get the primary group SID from the token
    dwLength = 0;
    GetTokenInformation(hToken.value(), TokenPrimaryGroup, NULL, 0, &dwLength);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetTokenInformation for primary group (sizing) [{}]", ec);
        return ec;
    }

    std::vector<BYTE> groupTokenBuffer(dwLength);
    if (!GetTokenInformation(hToken.value(), TokenPrimaryGroup, groupTokenBuffer.data(), dwLength, &dwLength))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed GetTokenInformation for primary group [{}]", ec);
        return ec;
    }

    PTOKEN_PRIMARY_GROUP pTokenGroup = reinterpret_cast<PTOKEN_PRIMARY_GROUP>(groupTokenBuffer.data());
    PSID pGroupSid = pTokenGroup->PrimaryGroup;

    // Create an explicit access structure for full control to the current user
    EXPLICIT_ACCESS_W ea = {};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(pUserSid);

    // Create a new DACL with only the current user having access
    PACL pAcl = NULL;
    DWORD dwRes = SetEntriesInAclW(1, &ea, NULL, &pAcl);
    if (dwRes != ERROR_SUCCESS)
    {
        auto ec = std::error_code(dwRes, std::system_category());
        Log::Error("Failed SetEntriesInAclW [{}]", ec);
        return ec;
    }

    std::unique_ptr<ACL, AclDeleter> aclGuard(pAcl);
    SECURITY_DESCRIPTOR sd = {};
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
    {
        auto ec = LastWin32Error();
        Log::Error("Failed InitializeSecurityDescriptor [{}]", ec);
        return ec;
    }

    if (!SetSecurityDescriptorDacl(&sd, TRUE, pAcl, FALSE))
    {
        auto ec = LastWin32Error();
        Log::Error("Failed SetSecurityDescriptorDacl [{}]", ec);
        return ec;
    }

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    if (!CreateDirectoryW(path.c_str(), &sa))
    {
        // Will also fail if already exists
        auto ec = LastWin32Error();
        Log::Debug(L"Failed CreateDirectoryW [{}]", ec);
        return ec;
    }

    // Must be useless, but double-check the ACLs
    auto rv = CheckDirectoryWithUserOnlyACL(path, pUserSid, FILE_ALL_ACCESS);
    if (rv.has_error() || rv.value() == false)
    {
        Log::Debug(L"Failed CheckDirectoryWithUserOnlyACL [{}]", rv.error());
        return rv.error();
    }

    return Orc::Success<void>();
}

}  // namespace

namespace Orc {

Result<std::wstring> CreateWorkingTemp(std::optional<std::wstring> basedir)
{
    if (!basedir || basedir->empty())
    {
        auto temp = GetTemporaryDirectory();
        if (!temp)
        {
            return temp.error();
        }

        basedir = *temp;
    }

    // It is difficult to keep the original behavior of temporary directory when defined using environment variables and
    // with overrides through child processes. This avoids multiple levels of 'WorkingTemp' nested directories.
    std::wstring workingTemp;
    if (!EndsWithNoCase(*basedir, std::wstring(kWorkingTempDirName)))
    {
        workingTemp = fmt::format(L"{}\\{}", *basedir, kWorkingTempDirName);
    }
    else
    {
        workingTemp = *basedir;
    }

    // Create a directory with default ACLs. It could be restricted but with an existing and permissive directory it
    // would require that content be cleaned/removed. And it would not be possible to always remove this directory as
    // 'working directory' is reused across multiple child tools.
    if (!CreateDirectoryW(workingTemp.c_str(), nullptr))
    {
        auto lastError = ::GetLastError();
        if (lastError != ERROR_ALREADY_EXISTS)
        {
            auto ec = Win32Error(lastError);
            Log::Debug(L"Failed CreateDirectoryW (path: {}) [{}]", workingTemp, ec);
            return ec;
        }
    }

    return workingTemp;
}

Result<std::wstring> CreateRestrictedDirectory(const std::wstring& workingTemp)
{
    // This check is to avoid any silent breaking change or misuse. As for now all the restricted directories are
    // expected to be direct subdirectory of 'WorkingTemp'. If relaxed be sure to avoid nested restricted directories or
    // other unwanted side effect.
    if (!EndsWithNoCase(workingTemp, std::wstring(kWorkingTempDirName)))
    {
        Log::Debug(L"Unexpected temporary directory format (path: {})", workingTemp);
        return std::errc::invalid_argument;
    }

    const std::filesystem::path path = fmt::format(L"{}\\{}.tmp", workingTemp, GetCurrentProcessId());

    auto rv = RemoveAll(path);
    if (!rv)
    {
        Log::Debug("Failed to remove existing restricted temporary directory (path: {})", path);
        return rv.error();
    }

    rv = CreateDirectoryWithUserOnlyACL(path);
    if (!rv)
    {
        Log::Debug(L"Failed to create restricted temporary directory (path: {}) [{}]", path, rv.error());
        return rv.error();
    }

    return path;
}

}  // namespace Orc
