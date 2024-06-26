//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"
#include "JobObject.h"

#include "Privilege.h"

#include "NtDllExtension.h"

#include "safeint.h"


#include <boost/scope_exit.hpp>

using namespace std;
using namespace boost;

using namespace Orc;

HRESULT
JobObject::GetHandle(DWORD dwSourcePid, HANDLE hSourceHandle, DWORD dwDesiredAccess, HANDLE& hHandle, bool bPreserveJob)
{
    HRESULT hr = E_FAIL;

    HANDLE hSourceProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, dwSourcePid);
    if (hSourceProcess == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed to open process (pid: {}, [{}])", dwSourcePid, SystemError(hr));
        return hr;
    }

    BOOST_SCOPE_EXIT((hSourceProcess)) { CloseHandle(hSourceProcess); }
    BOOST_SCOPE_EXIT_END;

    hHandle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(hSourceProcess, hSourceHandle, GetCurrentProcess(), &hHandle, dwDesiredAccess, FALSE, 0L))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed to duplicate handle [{}]", SystemError(hr));

        if (!bPreserveJob)
        {
            PSID pPrevOwner = NULL;
            if (FAILED(hr = TakeOwnership(SE_KERNEL_OBJECT, hSourceHandle, pPrevOwner)))
            {
                Log::Debug("Failed to take ownership of handle [{}]", SystemError(hr));
            }
            else
            {
                Log::Debug("Took ownership of handle, retry DuplicateHandle");

                PSID pMySid = NULL;
                if (FAILED(hr = GetMyCurrentSID(pMySid)))
                {
                    Log::Debug("Failed to get current user SID [{}]", SystemError(hr));
                }
                else
                {
                    if (FAILED(hr = ::GrantAccess(SE_KERNEL_OBJECT, hSourceHandle, pMySid, dwDesiredAccess)))
                    {
                        Log::Debug("Failed to grant access to source handle [{}]", SystemError(hr));
                    }
                    else
                    {
                        Log::Debug("Granted desired access to handle, retry DuplicateHandle");
                    }
                }
                if (!DuplicateHandle(
                        hSourceProcess, hSourceHandle, GetCurrentProcess(), &hHandle, dwDesiredAccess, FALSE, 0L))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Debug("Failed to duplicate handle (again...) [{}]", SystemError(hr));
                }
                else
                {
                    Log::Debug("DuplicateHandle succeeded after taking ownership");
                }
            }
        }
        return hr;
    }

    return S_OK;
}

HRESULT JobObject::GetHandleTypeName(DWORD dwSourcePid, HANDLE hSourceHandle, wstring& strType)
{
    HRESULT hr = E_FAIL;
    strType = L"(null)";

    HANDLE hHandle = INVALID_HANDLE_VALUE;
    if (FAILED(hr = GetHandle(dwSourcePid, hSourceHandle, GENERIC_READ, hHandle)))
        return hr;

    BOOST_SCOPE_EXIT((hHandle)) { CloseHandle(hHandle); }
    BOOST_SCOPE_EXIT_END;

    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();

    ULONG ulReturnedBytes = 0;
    hr = pNtDll->NtQueryObject(hHandle, ObjectTypeInformation, nullptr, 0L, &ulReturnedBytes);

    if (hr == HRESULT_FROM_NT(STATUS_INFO_LENGTH_MISMATCH))
    {
        PUBLIC_OBJECT_TYPE_INFORMATION* pTypeInfo = (PUBLIC_OBJECT_TYPE_INFORMATION*)malloc(ulReturnedBytes);

        BOOST_SCOPE_EXIT((pTypeInfo)) { free(pTypeInfo); }
        BOOST_SCOPE_EXIT_END;

        ZeroMemory(pTypeInfo, ulReturnedBytes);

        if (FAILED(
                hr = pNtDll->NtQueryObject(
                    hHandle, ObjectTypeInformation, pTypeInfo, ulReturnedBytes, &ulReturnedBytes)))
        {
            return hr;
        }

        strType.assign(pTypeInfo->TypeName.Buffer, pTypeInfo->TypeName.Length);
    }
    else if (FAILED(hr))
        return hr;
    return S_OK;
}

bool JobObject::IsProcessInJob(HANDLE hProcess)
{
    BOOL bIsProcessInJob = FALSE;

    if (m_hJob == INVALID_HANDLE_VALUE)
        return false;

    if (!::IsProcessInJob(hProcess == INVALID_HANDLE_VALUE ? GetCurrentProcess() : hProcess, m_hJob, &bIsProcessInJob))
    {
        Log::Error("Failed to determine if in job [{}]", LastWin32Error());
        return false;
    }
    return bIsProcessInJob == TRUE ? true : false;
}

HRESULT JobObject::GrantAccess(PSID pSid, ACCESS_MASK mask)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = ::GrantAccess(SE_KERNEL_OBJECT, m_hJob, pSid, mask)))
    {
        Log::Debug("First attempt to grant access right to job failed [{}]", SystemError(hr));
    }
    else
    {
        Log::Debug("First attempt to grant access right to job succeeded");
        return S_OK;
    }

    HANDLE hSettableHandle = INVALID_HANDLE_VALUE;
    if (FAILED(hr = GetHandle(GetCurrentProcessId(), m_hJob, WRITE_DAC, hSettableHandle)))
    {
        PSID pPreviousOwner = nullptr;
        if (FAILED(hr = TakeOwnership(SE_KERNEL_OBJECT, m_hJob, pPreviousOwner)))
        {
            Log::Debug("Failed to take ownership of job [{}]", SystemError(hr));
            return hr;
        }
        if (FAILED(hr = GetHandle(GetCurrentProcessId(), m_hJob, WRITE_DAC, hSettableHandle)))
        {
            Log::Debug("Failed to obtain settable job object [{}]", SystemError(hr));
            return hr;
        }
    }
    BOOST_SCOPE_EXIT(&hSettableHandle)
    {
        if (hSettableHandle != INVALID_HANDLE_VALUE)
            LocalFree(hSettableHandle);
        hSettableHandle = INVALID_HANDLE_VALUE;
    }
    BOOST_SCOPE_EXIT_END;

    if (FAILED(hr = ::GrantAccess(SE_KERNEL_OBJECT, hSettableHandle, pSid, mask)))
    {
        Log::Debug("Failed to grant access right to job [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT JobObject::GetJobObject(HANDLE hProcess, HANDLE& hJob)
{
    using namespace msl::utilities;
    HRESULT hr = E_FAIL;
    hJob = INVALID_HANDLE_VALUE;

    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();
    if (!pNtDll)
    {
        Log::Error("Could not load ntdll");
        return E_FAIL;
    }

    BOOL bIsProcessInJob = false;
    if (!::IsProcessInJob(hProcess, NULL, &bIsProcessInJob))
        return HRESULT_FROM_WIN32(GetLastError());

    if (!bIsProcessInJob)
    {
        Log::Debug("This process is not part of any job");
        return S_OK;
    }
    else
    {
        Log::Debug("This process is part of a job");
    }

    HANDLE hEmptyJob = CreateJobObject(NULL, L"OnlyToGetJobType");
    BOOST_SCOPE_EXIT(&hEmptyJob) { CloseHandle(hEmptyJob); }
    BOOST_SCOPE_EXIT_END;

    if (hEmptyJob == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Could not create empty job object [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = SetPrivilege(SE_DEBUG_NAME, TRUE)))
    {
        Log::Debug("Debug privilege is _not_ held [{}]", SystemError(hr));
    }

    ULONG ulNeededBytes = 0L;
    auto pHandleInfo = (SystemHandleInformationData*)::malloc(sizeof(SystemHandleInformationData));
    ULONG ulHandleInfoSize = (ULONG)sizeof(SystemHandleInformationData);

    if (pHandleInfo == nullptr)
        return E_OUTOFMEMORY;

    BOOST_SCOPE_EXIT(&pHandleInfo)
    {  // Ensure pHandleInfo is freed upon scope exit
        if (pHandleInfo)
            ::free(pHandleInfo);
    }
    BOOST_SCOPE_EXIT_END;

    while ((hr = pNtDll->NtQuerySystemInformation(
                (SYSTEM_INFORMATION_CLASS)SystemHandleInformation, pHandleInfo, ulHandleInfoSize, &ulNeededBytes))
           == HRESULT_FROM_NT(STATUS_INFO_LENGTH_MISMATCH))
    {
        auto pNewHandleInfo = (SystemHandleInformationData*)::realloc(pHandleInfo, SafeInt<size_t>(ulNeededBytes) * 2);
        if (pNewHandleInfo == nullptr)
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        pHandleInfo = pNewHandleInfo;
        ulHandleInfoSize = SafeInt<ULONG>(ulNeededBytes) * 2;
    }
    if (FAILED(hr))
    {
        Log::Debug("Failed to retrieve handle information [{}]", SystemError(hr));
        return hr;
    }

    UCHAR ucJobType = 0;
    DWORD dwProcessID = GetCurrentProcessId();

    Log::Debug("Found {} handles", pHandleInfo->ulCount);

    for (unsigned int i = 0; i < pHandleInfo->ulCount && ucJobType == 0; i++)
    {
        if (pHandleInfo->HandleInformation[i].ProcessID == dwProcessID
            && reinterpret_cast<HANDLE>(pHandleInfo->HandleInformation[i].HANDLE) == hEmptyJob)
        {
            Log::Debug("Job Type Number: {}", pHandleInfo->HandleInformation[i].ObjectTypeNumber);
            ucJobType = pHandleInfo->HandleInformation[i].ObjectTypeNumber;
            break;
        }
    }
    if (ucJobType == 0)
    {
        Log::Error("Could not determine job type by simple enumeration");
    }
    else
    {
        Log::Debug("Job type number is {}", ucJobType);
    }

    bool bPreserveJob = false;
    // Enable the SE_TAKE_OWNERSHIP_NAME privilege.
    if (FAILED(hr = SetPrivilege(SE_TAKE_OWNERSHIP_NAME, TRUE)))
    {
        bPreserveJob = true;
    }

    ULONG ulTriedHandles = 0, ulFailedHandles = 0;
    for (unsigned int i = 0; i < pHandleInfo->ulCount; i++)
    {
        if (ucJobType == pHandleInfo->HandleInformation[i].ObjectTypeNumber)
        {
            HANDLE hOtherJob = INVALID_HANDLE_VALUE;

            if (SUCCEEDED(GetHandle(
                    pHandleInfo->HandleInformation[i].ProcessID,
                    reinterpret_cast<HANDLE>(pHandleInfo->HandleInformation[i].HANDLE),
                    JOB_OBJECT_QUERY,
                    hOtherJob,
                    bPreserveJob)))
            {
                ulTriedHandles++;
                BOOL bIsProcessInJob = false;
                if (!::IsProcessInJob(hProcess, hOtherJob, &bIsProcessInJob))
                {
                    Log::Debug("Failed to determine if process is in this job");
                }
                if (bIsProcessInJob)
                {
                    hJob = hOtherJob;
                    return S_OK;
                }
                CloseHandle(hOtherJob);
            }
            else
                ulFailedHandles++;
        }
    }

    if (FAILED(hr = SetPrivilege(SE_DEBUG_NAME, FALSE)))
    {
        Log::Debug("Debug privilege is _not_ held");
    }

    if (bIsProcessInJob)
    {
        Log::Debug("Failed to find process job (handles tried: {}, failed: {})", ulTriedHandles, ulFailedHandles);
    }
    return S_FALSE;
}

JobObject JobObject::GetJobObject(HANDLE hProcess)
{
    HRESULT hr = E_FAIL;
    HANDLE hJob = INVALID_HANDLE_VALUE;

    if (SUCCEEDED(hr = JobObject::GetJobObject(hProcess, hJob)))
    {
        return JobObject(hJob);
    }
    return JobObject();
}

HRESULT Orc::JobObject::AssociateCompletionPort(HANDLE hCP, LPVOID pCompletionKey) const
{
    auto hr = E_FAIL;
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT JACP = {pCompletionKey, hCP};
    if (!SetInformationJobObject(m_hJob, JobObjectAssociateCompletionPortInformation, &JACP, sizeof(JACP)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to associate job object with completion port (job: {:p}, cp: {:p})", m_hJob, hCP);
        return hr;
    }
    return S_OK;
}

bool JobObject::IsBreakAwayAllowed()
{
    if (!IsValid())
        return false;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInfo;
    DWORD dwReturnedBytes = 0L;
    if (!QueryInformationJobObject(
            m_hJob,
            JobObjectExtendedLimitInformation,
            &LimitInfo,
            sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION),
            &dwReturnedBytes))
    {
        Log::Error("Failed to QueryInformationJobObject on job [{}]", LastWin32Error());
        return false;
    }
    if (LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK
        || LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
    {
        Log::Debug("Job verification is OK with breakaway");
        return true;
    }
    else
    {
        Log::Debug("Job configuration does not allow breakaway");
        return false;
    }
}

HRESULT JobObject::AllowBreakAway(bool bPreserveJob)
{
    HRESULT hr = E_FAIL;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInfo;
    ZeroMemory(&LimitInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));

    DWORD dwReturnedBytes = 0L;
    if (!QueryInformationJobObject(
            m_hJob,
            JobObjectExtendedLimitInformation,
            &LimitInfo,
            sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION),
            &dwReturnedBytes))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to QueryInformationJobObject on job [{}]", SystemError(hr));
        return hr;
    }
    if (LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK
        || LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
    {
        Log::Debug("Job already allows breakaway");
        return S_OK;
    }

    HANDLE hSettableJob = INVALID_HANDLE_VALUE;
    if (FAILED(
            hr = GetHandle(GetCurrentProcessId(), m_hJob, JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES, hSettableJob)))
    {
        if (bPreserveJob)
        {
            Log::Error("Failed to get a settable job [{}]", SystemError(hr));
            return hr;
        }
        else
        {
            PSID pMySID = nullptr;

            if (FAILED(hr = GetMyCurrentSID(pMySID)))
            {
                Log::Error(L"Failed to obtain my current SID [{}]", SystemError(hr));
                return hr;
            }
            BOOST_SCOPE_EXIT(&pMySID)
            {
                if (pMySID != nullptr)
                    LocalFree(pMySID);
                pMySID = nullptr;
            }
            BOOST_SCOPE_EXIT_END;

            if (FAILED(hr = GrantAccess(pMySID, JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES)))
            {
                Log::Error("Failed to grant myself acces to job [{}]", SystemError(hr));
                return hr;
            }
            if (FAILED(
                    hr = GetHandle(
                        GetCurrentProcessId(), m_hJob, JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES, hSettableJob)))
            {
                Log::Error("Failed to obtain a settable handle to the job [{}]", SystemError(hr));
                return hr;
            }
        }
    }
    BOOST_SCOPE_EXIT(&hSettableJob)
    {
        if (hSettableJob != INVALID_HANDLE_VALUE)
            CloseHandle(hSettableJob);
        hSettableJob = INVALID_HANDLE_VALUE;
    }
    BOOST_SCOPE_EXIT_END;

    LimitInfo.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if (!SetInformationJobObject(
            hSettableJob, JobObjectExtendedLimitInformation, &LimitInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to SetInformationJobObject on job [{}]", SystemError(hr));
        return hr;
    }
    return S_OK;
}

HRESULT JobObject::BlockBreakAway(bool bPreserveJob)
{
    DBG_UNREFERENCED_PARAMETER(bPreserveJob);
    HRESULT hr = E_FAIL;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInfo;
    ZeroMemory(&LimitInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));

    DWORD dwReturnedBytes = 0L;
    if (!QueryInformationJobObject(
            m_hJob,
            JobObjectExtendedLimitInformation,
            &LimitInfo,
            sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION),
            &dwReturnedBytes))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to QueryInformationJobObject on job [{}]", SystemError(hr));
        return hr;
    }
    if (!(LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK)
        && (LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))
    {
        Log::Debug("Job already block breakaway");
        return S_OK;
    }

    HANDLE hSettableJob = INVALID_HANDLE_VALUE;
    if (FAILED(
            hr = GetHandle(GetCurrentProcessId(), m_hJob, JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES, hSettableJob)))
    {
        Log::Error("Failed to get a settable job");
        return hr;
    }

    LimitInfo.BasicLimitInformation.LimitFlags =
        LimitInfo.BasicLimitInformation.LimitFlags & ~JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if (!SetInformationJobObject(
            hSettableJob, JobObjectExtendedLimitInformation, &LimitInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to SetInformationJobObject on job [{}]", SystemError(hr));
        CloseHandle(hSettableJob);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CloseHandle(hSettableJob);
    return S_OK;
}

HRESULT Orc::JobObject::Close()
{
    if (m_hJob != INVALID_HANDLE_VALUE)
    {
        if (!CloseHandle(m_hJob))
        {
            Log::Error("Failed CloseHandle for job [{}]", LastWin32Error());
        }

        m_hJob = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}

JobObject::~JobObject(void)
{
    if (m_bClose)
    {
        Close();
    }
}
