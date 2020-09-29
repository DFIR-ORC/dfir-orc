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

#include "CaseInsensitive.h"

#pragma managed(push, off)

namespace Orc {

using PidList = std::vector<DWORD>;

enum ModuleType
{
    MODULETYPE_NONE = 0,
    MODULETYPE_DLL = (1 << 0),
    MODULETYPE_EXE = (1 << 1),
    MODULETYPE_SYS = (1 << 2),
    MODULETYPE_ALL = 0xFFFFFFF
};

struct ModuleInfo
{
    ModuleType type;
    std::wstring strModule;
    PidList Pids;

    ModuleInfo()
        : type(MODULETYPE_NONE) {};
};

using ModuleMap = std::map<std::wstring, ModuleInfo, CaseInsensitive>;
using Modules = std::vector<ModuleInfo>;

using RunningCodeEnumItemCallback = void(const ModuleInfo& item, LPVOID pContext);

class ORCLIB_API RunningCode
{
private:
    ModuleMap m_ModMap;

    HRESULT EnumModules(DWORD dwPID);
    HRESULT EnumProcessesModules();
    HRESULT EnumDeviceDrivers();

    HRESULT SnapEnumProcessesModules();

public:
    HRESULT EnumRunningCode();

    HRESULT GetUniqueModules(Modules& modules, ModuleType type = MODULETYPE_ALL);
    bool IsModuleLoaded(const std::wstring& modulepath, ModuleType type = MODULETYPE_ALL);

    HRESULT WalkItems(RunningCodeEnumItemCallback pCallBack, LPVOID pContext);

    ~RunningCode(void);
};

}  // namespace Orc

#pragma managed(pop)
