//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <winternl.h>

#pragma managed(push, off)

namespace Orc {

static const auto SystemHandleInformation = 16;
static const auto SystemObjectInformation = 17;

struct SYSTEM_HANDLE_INFORMATON
{
    ULONG ProcessID;
    UCHAR ObjectTypeNumber;
    UCHAR Flags;
    USHORT HANDLE;
    PVOID Object;
    ACCESS_MASK GrantedAccess;
};

struct SystemHandleInformationData
{
    ULONG ulCount;
    SYSTEM_HANDLE_INFORMATON HandleInformation[1];
};

class JobObject
{
private:
    static HRESULT GetHandleTypeName(DWORD hSourcePid, HANDLE hSourceHandle, std::wstring& strType);
    static HRESULT GetHandle(
        DWORD hSourcePid,
        HANDLE hSourceHandle,
        DWORD dwDesiredAccess,
        HANDLE& hHandle,
        bool bPreserveJob = false);

    HANDLE m_hJob;
    bool m_bClose = false;

public:
    JobObject(HANDLE hJob = INVALID_HANDLE_VALUE)
        : m_hJob(hJob)
    {
    }
    JobObject(LPCWSTR szJobName)
        : m_hJob(INVALID_HANDLE_VALUE)
    {
        m_hJob = CreateJobObject(NULL, L"OnlyToGetJobType");
        if (m_hJob == NULL)
            m_hJob = INVALID_HANDLE_VALUE;
        else
            m_bClose = true;
    };

    JobObject(const JobObject& other) = delete;
    JobObject(JobObject&& other) noexcept
    {
        std::swap(m_bClose, other.m_bClose);
        std::swap(m_hJob, other.m_hJob);
    };
    JobObject& operator=(const JobObject& other) = delete;
    JobObject& operator=(JobObject&& other) noexcept
    {
        std::swap(m_bClose, other.m_bClose);
        std::swap(m_hJob, other.m_hJob);
        return *this;
    };

    bool operator==(const JobObject& other) const { return other.m_hJob == m_hJob; }
    bool operator!=(const JobObject& other) const { return other.m_hJob != m_hJob; }

    static HRESULT GetJobObject(HANDLE hProcess, HANDLE& hJob);
    static JobObject GetJobObject(HANDLE hProcess = INVALID_HANDLE_VALUE);

    HRESULT AssociateCompletionPort(HANDLE hCP, LPVOID pCompletionKey) const;

    HRESULT GrantAccess(PSID pSid, ACCESS_MASK mask);

    bool IsProcessInJob(HANDLE hProcess = INVALID_HANDLE_VALUE);

    bool IsValid() const { return m_hJob != INVALID_HANDLE_VALUE; };

    bool IsBreakAwayAllowed();

    HRESULT AllowBreakAway(bool bPreserveJob = false);
    HRESULT BlockBreakAway(bool bPreserveJob = false);

    HANDLE GetHandle() const { return m_hJob; }

    HRESULT Close();

    ~JobObject(void);
};

}  // namespace Orc

#pragma managed(pop)
