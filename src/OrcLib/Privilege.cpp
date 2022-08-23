//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Privilege.h"

#include <boost/scope_exit.hpp>

using namespace Orc;

HRESULT Orc::SetPrivilege(const WCHAR* szPrivilege, BOOL bEnablePrivilege)
{
    TOKEN_PRIVILEGES tp;
    LUID luid;
    HANDLE hToken = INVALID_HANDLE_VALUE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    BOOST_SCOPE_EXIT((&hToken))
    {
        if (hToken != INVALID_HANDLE_VALUE)
            CloseHandle(hToken);
        hToken = INVALID_HANDLE_VALUE;
    }
    BOOST_SCOPE_EXIT_END;

    if (!LookupPrivilegeValue(
            NULL,  // lookup privilege on local system
            szPrivilege,  // privilege to lookup
            &luid))  // receives LUID of privilege
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    // Enable the privilege or disable all privileges.

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
    {
        Log::Error(L"The required privilege '{}' is not held by the process token.", szPrivilege);
        return HRESULT_FROM_WIN32(ERROR_NOT_ALL_ASSIGNED);
    }

    return S_OK;
}

HRESULT Orc::GetMyCurrentSID(PSID& pSid)
{
    HRESULT hr = E_FAIL;
    DWORD dwSize = 0;

    // Open a handle to the access token for the calling process.
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed OpenProcessToken [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hToken))
    {
        if (hToken != INVALID_HANDLE_VALUE)
            CloseHandle(hToken);
        hToken = INVALID_HANDLE_VALUE;
    }
    BOOST_SCOPE_EXIT_END;

    // Call GetTokenInformation to get the buffer size.
    if (!GetTokenInformation(hToken, TokenUser, NULL, dwSize, &dwSize))
    {
        DWORD dwResult = GetLastError();
        if (dwResult != ERROR_INSUFFICIENT_BUFFER)
        {
            hr = HRESULT_FROM_WIN32(dwResult);
            Log::Error("Failed GetTokenInformation [{}]", SystemError(hr));
            return hr;
        }
    }

    // Allocate the buffer.
    PTOKEN_USER pUserInfo = (PTOKEN_USER)LocalAlloc(LPTR, dwSize);
    if (pUserInfo == nullptr)
        return E_OUTOFMEMORY;

    BOOST_SCOPE_EXIT((&pUserInfo))
    {
        LocalFree(pUserInfo);
        pUserInfo = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    // Call GetTokenInformation again to get the group information.
    if (!GetTokenInformation(hToken, TokenUser, pUserInfo, dwSize, &dwSize))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed GetTokenInformation [{}]", SystemError(hr));
        return hr;
    }

    if (!IsValidSid(pUserInfo->User.Sid))
    {
        Log::Error("Failed IsValidSid [{}]", SystemError(hr));
        return HRESULT_FROM_WIN32(ERROR_INVALID_SID);
    }

    DWORD dwSidLength = GetLengthSid(pUserInfo->User.Sid);
    if (dwSidLength == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed GetLengthSid [{}]", SystemError(hr));
        return hr;
    }

    pSid = (PSID)LocalAlloc(LPTR, dwSidLength);
    if (pSid == nullptr)
        return E_OUTOFMEMORY;

    if (!CopySid(dwSidLength, pSid, pUserInfo->User.Sid))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CopySid [{}]", SystemError(hr));
        LocalFree(pSid);
        return hr;
    }

#ifdef DEBUG
    LPWSTR pszSid = nullptr;
    if (!ConvertSidToStringSid(pSid, &pszSid))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed ConvertSidToStringSid [{}]", SystemError(hr));
        return S_OK;
    }

    Log::Debug(L"The current process token owner is '{}'", pszSid);
    LocalFree(pszSid);
#endif

    return S_OK;
}

HRESULT Orc::GetObjectOwnerSID(SE_OBJECT_TYPE objType, HANDLE hObject, PSID& pSid)
{
    HRESULT hr = S_OK;

    PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;

    if (!GetSecurityInfo(hObject, objType, OWNER_SECURITY_INFORMATION, &pSid, NULL, NULL, NULL, &pSecurityDescriptor))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed GetSecurityInfo [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Orc::TakeOwnership(SE_OBJECT_TYPE objType, HANDLE hObject, PSID& pPreviousOwnerSid)
{
    HRESULT hr = E_FAIL;

    PSID pMySID = nullptr;

    if (FAILED(hr = GetMyCurrentSID(pMySID)))
    {
        Log::Debug("Failed to obtain my current SID [{}]", SystemError(hr));
    }

    BOOST_SCOPE_EXIT(&pMySID)
    {
        if (pMySID != nullptr)
            LocalFree(pMySID);
        pMySID = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    PSID pCurrentOwnerSid = nullptr;
    PSECURITY_DESCRIPTOR pSecDescr = nullptr;
    if (!GetSecurityInfo(hObject, objType, TokenOwner, &pCurrentOwnerSid, NULL, NULL, NULL, &pSecDescr))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed to obtain current owner for object [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT(&pSecDescr)
    {
        if (pSecDescr != nullptr)
            LocalFree(pSecDescr);
        pSecDescr = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    if (pMySID != NULL && pCurrentOwnerSid != NULL)
    {
        if (EqualSid(pMySID, pCurrentOwnerSid))
        {
            // the SIDs are already equal, nothing else to do
            Log::Debug("SIDs are equal, already owner of this object");
            return S_OK;
        }
    }

    // Enable the SE_TAKE_OWNERSHIP_NAME privilege.
    if (FAILED(hr = SetPrivilege(SE_TAKE_OWNERSHIP_NAME, TRUE)))
    {
        Log::Debug("You must be logged on as Administrator [{}]", SystemError(hr));
        return hr;
    }

    // Set the owner in the object's security descriptor.
    if (!SetSecurityInfo(hObject, objType, OWNER_SECURITY_INFORMATION, pMySID, NULL, NULL, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed to change object's owner [{}]", SystemError(hr));
        return hr;
    }

    if (spdlog::default_logger()->level() == spdlog::level::debug)
    {
        LPWSTR pszSid = nullptr;
        if (!ConvertSidToStringSid(pMySID, &pszSid))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed ConvertSidToStringSid [{}]", SystemError(hr));
            return S_OK;
        }
        Log::Debug(L"The object's owner has been changed to '{}'", pszSid);
        LocalFree(pszSid);
    }

    // Disable the SE_TAKE_OWNERSHIP_NAME privilege.
    if (FAILED(hr = SetPrivilege(SE_TAKE_OWNERSHIP_NAME, FALSE)))
    {
        Log::Debug("Failed SetPrivilege to disable SE_TAKE_OWNERSHIP [{}]", SystemError(hr));
    }

    if (pCurrentOwnerSid != NULL)
    {
        DWORD dwSidLength = GetLengthSid(pCurrentOwnerSid);
        if (dwSidLength == 0)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed GetLengthSid [{}]", SystemError(hr));
            return hr;
        }

        pPreviousOwnerSid = (PSID)LocalAlloc(LPTR, dwSidLength);
        if (pPreviousOwnerSid == nullptr)
            return E_OUTOFMEMORY;

        if (!CopySid(dwSidLength, pPreviousOwnerSid, pCurrentOwnerSid))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed CopySid [{}]", SystemError(hr));
            LocalFree(pPreviousOwnerSid);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::GrantAccess(SE_OBJECT_TYPE objType, HANDLE hObject, PSID pSid, ACCESS_MASK mask)
{
    HRESULT hr = E_FAIL;

    // First task, get the DACL to modify
    PACL pCurrentDACL = nullptr;
    PSECURITY_DESCRIPTOR pSecDescr = nullptr;
    if (DWORD dwErrorCode =
            GetSecurityInfo(hObject, objType, TokenOwner, NULL, NULL, &pCurrentDACL, NULL, &pSecDescr) != ERROR_SUCCESS)
    {
        hr = HRESULT_FROM_WIN32(dwErrorCode);
        Log::Debug("Failed to obtain current DACL for object [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT(&pSecDescr)
    {
        if (pSecDescr != nullptr)
            LocalFree(pSecDescr);
        pSecDescr = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    EXPLICIT_ACCESS newRigths[1];

    newRigths[0].grfAccessPermissions = mask;
    newRigths[0].grfAccessMode = GRANT_ACCESS;
    newRigths[0].grfInheritance = NO_INHERITANCE;
    newRigths[0].Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    newRigths[0].Trustee.pMultipleTrustee = nullptr;
    newRigths[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    newRigths[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    newRigths[0].Trustee.ptstrName = (LPTSTR)pSid;

    PACL pNewACL = nullptr;
    if (!SetEntriesInAcl(1, newRigths, pCurrentDACL, &pNewACL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed SetEntriesInAcl to modify DACL [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT(&pNewACL)
    {
        if (pNewACL != nullptr)
            LocalFree(pNewACL);
        pNewACL = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    if (!SetSecurityInfo(hObject, objType, DACL_SECURITY_INFORMATION, NULL, NULL, pNewACL, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed to assign new ACL to object [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug("Successfully granted {:#x} access to object", mask);
    return S_OK;
}
