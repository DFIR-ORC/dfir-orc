//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>
#include <vector>
#include <map>
#include <string>
#include <functional>

#pragma managed(push, off)

namespace Orc {

struct ProcessInfo
{
    std::shared_ptr<std::wstring> strModule;
    DWORD m_Pid;
    DWORD m_ParentPid;

    ProcessInfo()
        : m_Pid(0)
        , m_ParentPid(0) {};
};

using PProcessInfo = ProcessInfo*;

using ProcessVector = std::vector<ProcessInfo>;

class RunningProcesses
{
private:
    ProcessVector m_Processes;

public:
    RunningProcesses() {}

    HRESULT EnumerateProcesses();

    HRESULT GetProcesses(ProcessVector& processes);

    bool IsProcessRunning(DWORD dwPid, DWORD dwParentId);
    DWORD GetProcessParentID(DWORD dwPid = 0);

    ~RunningProcesses(void);
};

}  // namespace Orc

#pragma managed(pop)
