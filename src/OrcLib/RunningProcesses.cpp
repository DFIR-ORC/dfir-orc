//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "RunningProcesses.h"

#include <TlHelp32.h>

using namespace std;

using namespace Orc;

HRESULT RunningProcesses::EnumProcesses()
{
    HRESULT hr = E_FAIL;
    // Print the name and process identifier for each process.
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Unable to create snapshot (code: {:#x})", hr);
        return hr;
    }

    PROCESSENTRY32W pe;
    // fill up its size
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe))
    {
        if (GetLastError() != ERROR_NO_MORE_FILES)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed Process32First (code: {:#x})", hr);
            return hr;
        }
    }

    BOOL retval = TRUE;
    while (retval)
    {
        pe.dwSize = sizeof(PROCESSENTRY32);

        if (pe.th32ProcessID != 0 && pe.th32ProcessID != 4 && pe.th32ProcessID != 8)
        {
            ProcessInfo info;
            info.m_ParentPid = pe.th32ParentProcessID;
            info.m_Pid = pe.th32ProcessID;
            info.strModule = make_shared<wstring>(pe.szExeFile);
            Log::Trace(L"EnumProcess '{}'", pe.szExeFile);
            m_Processes.push_back(info);
        }

        retval = Process32Next(hSnapshot, &pe);
        if (!retval)
        {
            if (GetLastError() != ERROR_NO_MORE_FILES)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error("Failed Process32Next (code: {:#x})", hr);
            }
        }
    }

    sort(m_Processes.begin(), m_Processes.end(), [](const ProcessInfo& info1, const ProcessInfo& info2) {
        return info1.m_Pid < info2.m_Pid;
    });

    // close snapshot handle
    CloseHandle(hSnapshot);
    return S_OK;
}

HRESULT RunningProcesses::GetProcesses(ProcessVector& processes)
{
    processes.clear();

    processes.insert(processes.end(), m_Processes.begin(), m_Processes.end());

    return S_OK;
}

bool RunningProcesses::IsProcessRunning(DWORD dwPid, DWORD dwParentPid)
{
    ProcessInfo info;
    info.m_ParentPid = dwParentPid;
    info.m_Pid = dwPid;

    auto it = lower_bound(
        m_Processes.begin(), m_Processes.end(), info, [](const ProcessInfo& item1, const ProcessInfo& item2) {
            return item1.m_ParentPid == item2.m_ParentPid && item1.m_Pid == item2.m_Pid;
        });
    return it != m_Processes.end();
}

DWORD RunningProcesses::GetProcessParentID(DWORD dwPid)
{
    if (dwPid == 0)
        dwPid = GetCurrentProcessId();

    auto result = std::find_if(m_Processes.cbegin(), m_Processes.cend(), [dwPid](const ProcessInfo& item) -> bool {
        return item.m_Pid == dwPid;
    });

    if (result != m_Processes.end())
        return result->m_ParentPid;
    else
        return 0;
}

RunningProcesses::~RunningProcesses(void) {}
