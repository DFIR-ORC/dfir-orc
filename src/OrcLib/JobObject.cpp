//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"
#include "JobObject.h"

#include "LogFileWriter.h"

#include "Privilege.h"

#include "NtDllExtension.h"

#include "safeint.h"

static const auto STATUS_INFO_LENGTH_MISMATCH = ((NTSTATUS)0xC0000004L);

#include <boost/scope_exit.hpp>

using namespace std;
using namespace boost;

using namespace Orc;

HRESULT JobObject::GetHandle(
    const logger& pLog,
    DWORD dwSourcePid,
    HANDLE hSourceHandle,
    DWORD dwDesiredAccess,
    HANDLE& hHandle,
    bool bPreserveJob)
{
    HRESULT hr = E_FAIL;

    HANDLE hSourceProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, dwSourcePid);
    if (hSourceProcess == NULL)
    {
        log::Verbose(
            pLog,
            L"Failed to open process (pid=%d, hr=0x%lx)\r\n",
            dwSourcePid,
            hr = HRESULT_FROM_WIN32(GetLastError()));
        return hr;
    }

    BOOST_SCOPE_EXIT((hSourceProcess)) { CloseHandle(hSourceProcess); }
    BOOST_SCOPE_EXIT_END;

    hHandle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(hSourceProcess, hSourceHandle, GetCurrentProcess(), &hHandle, dwDesiredAccess, FALSE, 0L))
    {
        log::Verbose(pLog, L"Failed to duplicate handle (hr=0x%lx)\r\n", hr = HRESULT_FROM_WIN32(GetLastError()));
        if (!bPreserveJob)
        {
            PSID pPrevOwner = NULL;
            if (FAILED(hr = TakeOwnership(pLog, SE_KERNEL_OBJECT, hSourceHandle, pPrevOwner)))
            {
                log::Verbose(pLog, L"Failed to take ownership of handle (hr=0x%lx)\r\n", hr);
            }
            else
            {
                log::Verbose(pLog, L"Took ownership of handle, retry DuplicateHandle\r\n");

                PSID pMySid = NULL;
                if (FAILED(hr = GetMyCurrentSID(pLog, pMySid)))
                {
                    log::Verbose(
                        pLog,
                        L"Failed to take ownership of handle (hr=0x%lx)\r\n",
                        hr = HRESULT_FROM_WIN32(GetLastError()));
                }
                else
                {
                    if (FAILED(hr = ::GrantAccess(pLog, SE_KERNEL_OBJECT, hSourceHandle, pMySid, dwDesiredAccess)))
                    {
                        log::Verbose(
                            pLog,
                            L"Failed to take ownership of handle (hr=0x%lx)\r\n",
                            hr = HRESULT_FROM_WIN32(GetLastError()));
                    }
                    else
                    {
                        log::Verbose(pLog, L"Granted desired access to handle, retry DuplicateHandle\r\n");
                    }
                }
                if (!DuplicateHandle(
                        hSourceProcess, hSourceHandle, GetCurrentProcess(), &hHandle, dwDesiredAccess, FALSE, 0L))
                {
                    log::Verbose(
                        pLog,
                        L"Failed to duplicate handle (again...) (hr=0x%lx)\r\n",
                        hr = HRESULT_FROM_WIN32(GetLastError()));
                }
                else
                {
                    log::Verbose(pLog, L"DuplicateHandle succeeded after taking ownership\r\n");
                }
            }
        }
        return hr;
    }

    return S_OK;
}

HRESULT JobObject::GetHandleTypeName(const logger& pLog, DWORD dwSourcePid, HANDLE hSourceHandle, wstring& strType)
{
    HRESULT hr = E_FAIL;
    strType = L"(null)";

    HANDLE hHandle = INVALID_HANDLE_VALUE;
    if (FAILED(hr = GetHandle(pLog, dwSourcePid, hSourceHandle, GENERIC_READ, hHandle)))
        return hr;

    BOOST_SCOPE_EXIT((hHandle)) { CloseHandle(hHandle); }
    BOOST_SCOPE_EXIT_END;

    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>(pLog);

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
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to determine if in job\r\n");
        return false;
    }
    return bIsProcessInJob == TRUE ? true : false;
}

HRESULT JobObject::GrantAccess(PSID pSid, ACCESS_MASK mask)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = ::GrantAccess(_L_, SE_KERNEL_OBJECT, m_hJob, pSid, mask)))
    {
        log::Verbose(_L_, L"First attempt to grant access rigth to job failed (hr=0x%lx)\r\n", hr);
    }
    else
    {
        log::Verbose(_L_, L"First attempt to grant access rigth to job succeeded\r\n");
        return S_OK;
    }

    HANDLE hSettableHandle = INVALID_HANDLE_VALUE;
    if (FAILED(hr = GetHandle(_L_, GetCurrentProcessId(), m_hJob, WRITE_DAC, hSettableHandle)))
    {
        PSID pPreviousOwner = nullptr;
        if (FAILED(hr = TakeOwnership(_L_, SE_KERNEL_OBJECT, m_hJob, pPreviousOwner)))
        {
            log::Verbose(_L_, L"Failed to take ownership of job\r\n");
            return hr;
        }
        if (FAILED(hr = GetHandle(_L_, GetCurrentProcessId(), m_hJob, WRITE_DAC, hSettableHandle)))
        {
            log::Verbose(_L_, L"Failed to obtain settable job object\r\n");
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

    if (FAILED(hr = ::GrantAccess(_L_, SE_KERNEL_OBJECT, hSettableHandle, pSid, mask)))
    {
        log::Verbose(_L_, L"Failed to grant access rigth to job\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT JobObject::GetJobObject(const logger& pLog, HANDLE hProcess, HANDLE& hJob)
{
    using namespace msl::utilities;
    HRESULT hr = E_FAIL;
    hJob = INVALID_HANDLE_VALUE;

    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>(pLog);
    if (!pNtDll)
    {
        log::Error(pLog, E_FAIL, L"Could not load ntdll\r\n");
        return E_FAIL;
    }

    BOOL bIsProcessInJob = false;
    if (!::IsProcessInJob(hProcess, NULL, &bIsProcessInJob))
        return HRESULT_FROM_WIN32(GetLastError());

    if (!bIsProcessInJob)
    {
        log::Verbose(pLog, L"This process is not part of any job\r\n");
        return S_OK;
    }
    else
    {
        log::Verbose(pLog, L"This process is part of a job\r\n");
    }

    HANDLE hEmptyJob = CreateJobObject(NULL, L"OnlyToGetJobType");
    BOOST_SCOPE_EXIT(&hEmptyJob) { CloseHandle(hEmptyJob); }
    BOOST_SCOPE_EXIT_END;

    if (hEmptyJob == INVALID_HANDLE_VALUE)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Could not create empty job object\r\n");
        return hr;
    }

    if (FAILED(hr = SetPrivilege(pLog, SE_DEBUG_NAME, TRUE)))
    {
        log::Verbose(pLog, L"Debug privilege is _not_ held\r\n");
    }

    ULONG ulNeededBytes = 0L;
    auto pHandleInfo = (SystemHandleInformationData*)::malloc(sizeof(SystemHandleInformationData));
    ULONG ulHandleInfoSize = (ULONG)sizeof(SystemHandleInformationData);

    if (pHandleInfo == nullptr)
        return E_OUTOFMEMORY;

    BOOST_SCOPE_EXIT(&pHandleInfo) { // Ensure pHandleInfo is freed upon scope exit
        if(pHandleInfo)
           ::free(pHandleInfo);
    } BOOST_SCOPE_EXIT_END;

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
        log::Verbose(pLog, L"Failed to retrieve handle information (hr = 0x%lx)\r\n", hr);
        return hr;
    }
    
    UCHAR ucJobType = 0;
    DWORD dwProcessID = GetCurrentProcessId();

    log::Verbose(pLog, L"Found %d handles\r\n", pHandleInfo->ulCount);

    for (unsigned int i = 0; i < pHandleInfo->ulCount && ucJobType == 0; i++)
    {
        if (pHandleInfo->HandleInformation[i].ProcessID == dwProcessID
            && ((HANDLE)pHandleInfo->HandleInformation[i].HANDLE == hEmptyJob))
        {
            log::Verbose(pLog, L"Job Type Number= %d\r\n", pHandleInfo->HandleInformation[i].ObjectTypeNumber);
            ucJobType = pHandleInfo->HandleInformation[i].ObjectTypeNumber;
            break;
        }
    }
    if (ucJobType == 0)
    {
        log::Error(pLog, E_FAIL, L"Could not determine job type by simple enumeration\r\n");
    }
    else
    {
        log::Verbose(pLog, L"Job type number is %d\r\n", ucJobType);
    }

    bool bPreserveJob = false;
    // Enable the SE_TAKE_OWNERSHIP_NAME privilege.
    if (FAILED(hr = SetPrivilege(pLog, SE_TAKE_OWNERSHIP_NAME, TRUE)))
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
                    pLog,
                    pHandleInfo->HandleInformation[i].ProcessID,
                    (HANDLE)pHandleInfo->HandleInformation[i].HANDLE,
                    JOB_OBJECT_QUERY,
                    hOtherJob,
                    bPreserveJob)))
            {
                ulTriedHandles++;
                BOOL bIsProcessInJob = false;
                if (!::IsProcessInJob(hProcess, hOtherJob, &bIsProcessInJob))
                {
                    log::Verbose(pLog, L"Failed to determine if process is in this job\r\n");
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

    if (FAILED(hr = SetPrivilege(pLog, SE_DEBUG_NAME, FALSE)))
    {
        log::Verbose(pLog, L"Debug privilege is _not_ held\r\n");
    }

    if (bIsProcessInJob)
    {
        log::Verbose(
            pLog,
            L"This process was not found part of any of the jobs enumerated (but runs within one, tried %d job "
            L"handles, failed %d ones)\r\n",
            ulTriedHandles,
            ulFailedHandles);
    }
    return S_FALSE;
}

JobObject JobObject::GetJobObject(const logger& pLog, HANDLE hProcess)
{
    HRESULT hr = E_FAIL;
    HANDLE hJob = INVALID_HANDLE_VALUE;

    if (SUCCEEDED(hr = JobObject::GetJobObject(pLog, hProcess, hJob)))
    {
        return JobObject(pLog, hJob);
    }
    return JobObject(pLog);
}

HRESULT Orc::JobObject::AssociateCompletionPort(HANDLE hCP, LPVOID pCompletionKey) const
{
    auto hr = E_FAIL;
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT JACP = {pCompletionKey, hCP};
    if (!SetInformationJobObject(m_hJob, JobObjectAssociateCompletionPortInformation, &JACP, sizeof(JACP)))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to associate job object with completion port (job 0x%lX, cp 0x%lX)\r\n",
            m_hJob,
            hCP);
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
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to QueryInformationJobObject on job\r\n");
        return false;
    }
    if (LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK
        || LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
    {
        log::Verbose(_L_, L"Job verification is OK with breakaway\r\n");
        return true;
    }
    else
    {
        log::Verbose(_L_, L"Job configuration does not allow breakaway\r\n");
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
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to QueryInformationJobObject on job\r\n");
        return hr;
    }
    if (LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK
        || LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)
    {
        log::Verbose(_L_, L"Job already allows breakaway\r\n");
        return S_OK;
    }

    HANDLE hSettableJob = INVALID_HANDLE_VALUE;
    if (FAILED(
            hr = GetHandle(
                _L_, GetCurrentProcessId(), m_hJob, JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES, hSettableJob)))
    {
        if (bPreserveJob)
        {
            log::Error(_L_, hr, L"Failed to get a settable job\r\n");
            return hr;
        }
        else
        {
            PSID pMySID = nullptr;

            if (FAILED(hr = GetMyCurrentSID(_L_, pMySID)))
            {
                log::Error(_L_, hr, L"Failed to obtain my current SID\r\n");
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
                log::Error(_L_, hr, L"Failed to grant myself acces to job\r\n");
                return hr;
            }
            if (FAILED(
                    hr = GetHandle(
                        _L_,
                        GetCurrentProcessId(),
                        m_hJob,
                        JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES,
                        hSettableJob)))
            {
                log::Error(_L_, hr, L"Failed to obtain a settable handle to the job\r\n");
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
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to SetInformationJobObject on job\r\n");
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
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to QueryInformationJobObject on job\r\n");
        return hr;
    }
    if (!(LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK)
        && (LimitInfo.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))
    {
        log::Verbose(_L_, L"INFO: Job already block breakaway\r\n");
        return S_OK;
    }

    HANDLE hSettableJob = INVALID_HANDLE_VALUE;
    if (FAILED(
            hr = GetHandle(
                _L_, GetCurrentProcessId(), m_hJob, JOB_OBJECT_QUERY | JOB_OBJECT_SET_ATTRIBUTES, hSettableJob)))
    {
        log::Error(_L_, hr, L"Failed to get a settable job\r\n");
        return hr;
    }

    LimitInfo.BasicLimitInformation.LimitFlags =
        LimitInfo.BasicLimitInformation.LimitFlags & ~JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    if (!SetInformationJobObject(
            hSettableJob, JobObjectExtendedLimitInformation, &LimitInfo, sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to SetInformationJobObject on job\r\n");
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
        CloseHandle(m_hJob);
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
