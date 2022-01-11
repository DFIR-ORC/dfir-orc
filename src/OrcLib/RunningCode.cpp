//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "RunningCode.h"

#include "PSAPIExtension.h"
#include "Privilege.h"

#include <psapi.h>

#include <TlHelp32.h>

#include <vector>
#include <map>
#include <algorithm>

#include <boost\scope_exit.hpp>

using namespace std;

using namespace Orc;

constexpr auto ENUM_PROCESS_BASE_COUNT = 1024;
constexpr auto ENUM_PROCESS_BASE_INCR = 512;

HRESULT RunningCode::EnumerateProcessesModules()
{
    SetPrivilege(SE_DEBUG_NAME, TRUE);

    DWORD dwProcesses = ENUM_PROCESS_BASE_COUNT;
    DWORD* pProcesses = nullptr;

    BOOST_SCOPE_EXIT(&pProcesses)
    {
        if (pProcesses != nullptr)
            free(pProcesses);
        pProcesses = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    do
    {
        pProcesses = (DWORD*)malloc(dwProcesses);
        if (pProcesses == nullptr)
            return E_OUTOFMEMORY;

        DWORD dwThisTry = dwProcesses;
        if (!::EnumProcesses(pProcesses, dwThisTry, &dwProcesses))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (dwThisTry <= dwProcesses)
        {
            dwProcesses += ENUM_PROCESS_BASE_INCR;
            free(pProcesses);
            pProcesses = nullptr;
        }
    } while (pProcesses == nullptr);

    HRESULT hr = S_OK;

    for (unsigned int i = 0; i < (dwProcesses / sizeof(DWORD)); i++)
    {
        HRESULT hr2 = E_FAIL;
        if (FAILED(hr2 = EnumerateModules(pProcesses[i])))
        {
            Log::Debug("Could not load modules for process: {} [{}]", pProcesses[i], SystemError(hr2));
        }
    }
    return hr;
}

constexpr auto ENUM_MODULES_BASE_COUNT = 1024;

HRESULT RunningCode::EnumerateModules(DWORD dwPID)
{
    HRESULT hr = E_FAIL;
    HANDLE hProcess = INVALID_HANDLE_VALUE;

    const auto psapi = ExtensionLibrary::GetLibrary<PSAPIExtension>();
    if (!psapi)
    {
        Log::Debug("EnumModules: Failed to load PSAPI");
        return E_FAIL;
    }

    if ((hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID)) == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("EnumModules: OpenProcess failed for pid {} [{}]", dwPID, SystemError(hr));
        return hr;
    }

    DWORD dwModules = ENUM_MODULES_BASE_COUNT;
    HMODULE* phModules = nullptr;

    BOOST_SCOPE_EXIT(&phModules, &hProcess)
    {
        if (hProcess != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hProcess);
            hProcess = INVALID_HANDLE_VALUE;
        }

        if (phModules != nullptr)
            free(phModules);
        phModules = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    do
    {
        phModules = (HMODULE*)calloc(dwModules, sizeof(HMODULE));
        if (phModules == nullptr)
            return E_OUTOFMEMORY;

        DWORD cbThisTry = dwModules * sizeof(HMODULE);
        DWORD cbNeeded = 0LU;
        if (FAILED(hr = psapi->EnumProcessModules(hProcess, phModules, cbThisTry, &cbNeeded)))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (cbThisTry < cbNeeded)
        {
            free(phModules);
            phModules = nullptr;
            dwModules = cbNeeded / sizeof(HMODULE);
            _ASSERT(cbNeeded % sizeof(HMODULE) == 0);
        }
    } while (phModules == nullptr);

    for (unsigned int i = 0; i < dwModules; i++)
    {
        ModuleInfo modinfo;
        modinfo.type = MODULETYPE_DLL;

        WCHAR szFileNameEx[MAX_PATH];
        GetModuleFileNameEx(hProcess, phModules[i], szFileNameEx, MAX_PATH);

        modinfo.strModule = szFileNameEx;

        auto item = m_ModMap.find(modinfo.strModule);

        if (item != m_ModMap.end())
        {
            // this module was already listed
            item->second.Pids.push_back(dwPID);
        }
        else
        {
            // this module was unknown (yet)
            modinfo.Pids.push_back(dwPID);
            m_ModMap.insert(pair<std::wstring, ModuleInfo>(modinfo.strModule, modinfo));
            Log::Trace(L"EnumModule: {}", modinfo.strModule);
        }
    }
    phModules = nullptr;
    return S_OK;
}

#define ENUM_DEVICEDRIVERS_BASE_COUNT 1024
#define ENUM_DEVICEDRIVERS_BASE_INCR 512

HRESULT RunningCode::EnumerateDeviceDrivers()
{
    DWORD dwDeviceDrivers = ENUM_DEVICEDRIVERS_BASE_COUNT;
    LPVOID* phDeviceDrivers = nullptr;

    BOOST_SCOPE_EXIT(&phDeviceDrivers)
    {
        if (phDeviceDrivers != nullptr)
            free(phDeviceDrivers);
        phDeviceDrivers = nullptr;
    }
    BOOST_SCOPE_EXIT_END;

    do
    {
        phDeviceDrivers = (LPVOID*)malloc(dwDeviceDrivers);
        if (phDeviceDrivers == nullptr)
            return E_OUTOFMEMORY;

        DWORD dwThisTry = dwDeviceDrivers;
        if (!::EnumDeviceDrivers(phDeviceDrivers, dwThisTry, &dwDeviceDrivers))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (dwThisTry <= dwDeviceDrivers)
        {
            dwDeviceDrivers += ENUM_DEVICEDRIVERS_BASE_INCR;
            free(phDeviceDrivers);
            phDeviceDrivers = NULL;
        }
    } while (phDeviceDrivers == nullptr);

    for (unsigned int i = 0; i < dwDeviceDrivers / sizeof(LPVOID); i++)
    {
        ModuleInfo modinfo;
        modinfo.type = MODULETYPE_SYS;

        WCHAR szDriverFileName[MAX_PATH];
        DWORD dwLen = GetDeviceDriverFileName(phDeviceDrivers[i], szDriverFileName, MAX_PATH);
        modinfo.strModule = szDriverFileName;
        if (dwLen == 0L)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        if (dwLen < MAX_PATH - 1)
        {
            auto item = m_ModMap.find(modinfo.strModule);

            if (item == m_ModMap.end())
            {
                m_ModMap.insert(pair<wstring, ModuleInfo>(modinfo.strModule, modinfo));
                Log::Trace(L"EnumDeviceDriver '{}'", modinfo.strModule);
            }

            return S_OK;
        }

        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT RunningCode::SnapEnumProcessesModules()
{
    HRESULT hr = E_FAIL;
    // Print the name and process identifier for each process.
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Unable to create snapshot [{}]", SystemError(hr));
        return hr;
    }

    MODULEENTRY32 me32;
    //  Set the size of the structure before using it.
    me32.dwSize = sizeof(MODULEENTRY32);
    if (!Module32First(hSnapshot, &me32))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed Module32First [{}]", SystemError(hr));
        CloseHandle(hSnapshot);
        return hr;
    }

    do
    {

        ModuleInfo modinfo;
        modinfo.type = MODULETYPE_DLL;

        modinfo.strModule = me32.szExePath;

        auto item = m_ModMap.find(modinfo.strModule);

        if (item != m_ModMap.end())
        {
            // this module was already listed
            item->second.Pids.push_back(me32.th32ProcessID);
        }
        else
        {
            // this module was unknown (yet)
            modinfo.Pids.push_back(me32.th32ProcessID);
            m_ModMap.insert(pair<std::wstring, ModuleInfo>(modinfo.strModule, modinfo));
            Log::Trace(L"SnapEnumModule '{}'", modinfo.strModule);
        }

    } while (Module32Next(hSnapshot, &me32));

    // close snapshot handle
    CloseHandle(hSnapshot);
    return S_OK;
}

HRESULT RunningCode::EnumRunningCode()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = EnumerateProcessesModules()))
    {
        Log::Error("Failed to enumerate process's modules [{}]", SystemError(hr));
    }
    if (FAILED(hr = EnumerateDeviceDrivers()))
    {
        Log::Error("Failed to enumerate device drivers [{}]", SystemError(hr));
    }
    if (FAILED(hr = SnapEnumProcessesModules()))
    {
        Log::Error("Failed to enumerate SNAPSHOT modules [{}]", SystemError(hr));
    }
    return S_OK;
}

HRESULT RunningCode::GetUniqueModules(Modules& modules, ModuleType type)
{
    modules.clear();
    modules.resize(m_ModMap.size());

    // Walk through the files
    auto iter = modules.begin();

    std::for_each(begin(m_ModMap), end(m_ModMap), [&iter, type](const std::pair<std::wstring, ModuleInfo>& item) {
        if (item.second.type & type)
        {
            *(iter++) = item.second;
        }
    });

    std::sort(begin(modules), end(modules), [](const ModuleInfo& m1, const ModuleInfo& m2) {
        return std::lexicographical_compare(
            begin(m1.strModule),
            end(m1.strModule),  // source range
            begin(m2.strModule),
            end(m2.strModule),  // dest range
            [](const wchar_t& c1, const wchar_t& c2) { return tolower(c1) < tolower(c2); });
    });

    auto new_end = std::unique(begin(modules), end(modules), [](const ModuleInfo& m1, const ModuleInfo& m2) {
        return std::equal(
            begin(m1.strModule),
            end(m1.strModule),  // source range
            begin(m2.strModule),
            end(m2.strModule),  // dest range
            [](const wchar_t& c1, const wchar_t& c2) { return tolower(c1) == tolower(c2); });
    });

    modules.erase(new_end, end(modules));

    for (auto& mod : modules)
    {
        std::sort(begin(mod.Pids), end(mod.Pids));
        auto new_end = std::unique(begin(mod.Pids), end(mod.Pids));
        mod.Pids.erase(new_end, end(mod.Pids));
    }
    modules.shrink_to_fit();
    return S_OK;
}

bool RunningCode::IsModuleLoaded(const std::wstring& modulepath, ModuleType type)
{
    auto it = m_ModMap.find(modulepath);

    if (it == m_ModMap.end())
        return false;

    if (it->second.type & type)
        return true;

    return false;
}

HRESULT RunningCode::WalkItems(RunningCodeEnumItemCallback pCallback, LPVOID pContext)
{
    if (pCallback == NULL)
        return E_POINTER;

    // Walk through the files
    for (auto iter = m_ModMap.begin(); iter != m_ModMap.end(); ++iter)
    {
        pCallback(iter->second, pContext);
    }

    return S_OK;
}

RunningCode::~RunningCode(void)
{
    m_ModMap.clear();
}
