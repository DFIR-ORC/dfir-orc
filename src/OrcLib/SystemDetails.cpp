//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "SystemDetails.h"

#include <filesystem>

#include <WinNls.h>
#include <WinError.h>
#include <winsock.h>
#include <winsock2.h>
#include <iphlpapi.h>

#include <boost/scope_exit.hpp>

#include "TableOutput.h"
#include "WideAnsi.h"
#include "WMIUtil.h"
#include "BinaryBuffer.h"
#include "Utils/Time.h"
#include "Utils/TypeTraits.h"

namespace fs = std::filesystem;
using namespace std::string_view_literals;
using namespace Orc;

namespace Orc {
struct SystemDetailsBlock
{
    OSVERSIONINFOEX osvi;
    SYSTEM_INFO si;
    bool WOW64 = false;
    std::wstring strOSDescription;
    std::wstring strComputerName;
    std::wstring strFullComputerName;
    std::wstring strTimeStamp;
    Traits::TimeUtc<SYSTEMTIME> timestamp;
    std::optional<std::wstring> strOrcComputerName;
    std::optional<std::wstring> strOrcFullComputerName;
    std::optional<std::wstring> strProductType;
    std::optional<BYTE> wProductType;
    std::wstring strUserName;
    std::wstring strUserSID;
    std::optional<SystemTags> Tags;
    bool bIsElevated = false;
    DWORD dwLargePageSize = 0L;
    WMI wmi;
};
}  // namespace Orc

std::unique_ptr<SystemDetailsBlock> g_pDetailsBlock;

constexpr auto BUFSIZE = 256;

using PGNSI = void(WINAPI*)(LPSYSTEM_INFO);
using PGPI = BOOL(WINAPI*)(DWORD, DWORD, DWORD, DWORD, PDWORD);

using LPFN_ISWOW64PROCESS = BOOL(WINAPI*)(HANDLE, PBOOL);
using LPFN_GETLARGEPAGEMINIMUM = BOOL(WINAPI*)();

HRESULT Orc::SystemDetails::SetSystemType(std::wstring strProductType)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (!strProductType.empty())
        g_pDetailsBlock->strProductType.emplace(std::move(strProductType));

    return S_OK;
}

HRESULT SystemDetails::GetSystemType(std::wstring& strProductType)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (g_pDetailsBlock->strProductType.has_value())
    {
        strProductType = g_pDetailsBlock->strProductType.value();
        return S_OK;
    }

    switch (g_pDetailsBlock->osvi.wProductType)
    {
        case VER_NT_WORKSTATION:
            strProductType = L"WorkStation";
            break;
        case VER_NT_SERVER:
            strProductType = L"Server";
            break;
        case VER_NT_DOMAIN_CONTROLLER:
            strProductType = L"DomainController";
            break;
    }
    return S_OK;
}

HRESULT Orc::SystemDetails::GetSystemType(BYTE& systemType)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    systemType = g_pDetailsBlock->osvi.wProductType;
    return S_OK;
}

const SystemTags& Orc::SystemDetails::GetSystemTags()
{
    using namespace std::string_literals;

    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        throw Exception(Severity::Fatal, L"System details information could not be loaded"sv);

    if (g_pDetailsBlock->Tags.has_value())
        return g_pDetailsBlock->Tags.value();

    g_pDetailsBlock->Tags.emplace();

    auto& tags = g_pDetailsBlock->Tags.value();
    WORD wArch = 0;
    GetArchitecture(wArch);
    switch (wArch)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            tags.insert(L"x86"s);
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            tags.insert(L"x64"s);
            break;
        default:
            throw Exception(Severity::Fatal, L"Unsupported architechture {}"sv, wArch);
    }

    auto [major, minor] = Orc::SystemDetails::GetOSVersion();

    BYTE systemType = 0L;
    if (auto hr = Orc::SystemDetails::GetSystemType(systemType); FAILED(hr))
        throw Exception(Severity::Fatal, hr, L"System type not available"sv);

    switch (major)
    {
        case 5:
            switch (minor)
            {
                case 1:
                case 2:
                    switch (systemType)
                    {
                        case VER_NT_WORKSTATION:
                            tags.insert(L"WindowsXP"s);
                            break;
                        case VER_NT_SERVER:
                        case VER_NT_DOMAIN_CONTROLLER:
                            tags.insert(L"WindowsServer2003"s);
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        case 6:
            switch (minor)
            {
                case 0:
                    switch (systemType)
                    {
                        case VER_NT_WORKSTATION:
                            tags.insert(L"WindowsVista"s);
                            break;
                        case VER_NT_SERVER:
                        case VER_NT_DOMAIN_CONTROLLER:
                            tags.insert(L"WindowsServer2008"s);
                            break;
                    }
                    break;
                case 1:
                    switch (systemType)
                    {
                        case VER_NT_WORKSTATION:
                            tags.insert(L"Windows7"s);
                            break;
                        case VER_NT_SERVER:
                        case VER_NT_DOMAIN_CONTROLLER:
                            tags.insert(L"WindowsServer2008R2"s);
                            break;
                    }
                    break;
                case 2:
                    switch (systemType)
                    {
                        case VER_NT_WORKSTATION:
                            tags.insert(L"Windows8"s);
                            break;
                        case VER_NT_SERVER:
                        case VER_NT_DOMAIN_CONTROLLER:
                            tags.insert(L"WindowsServer2012"s);
                            break;
                    }
                    break;
                case 3:
                    switch (systemType)
                    {
                        case VER_NT_WORKSTATION:
                            tags.insert(L"Windows8.1"s);
                            break;
                        case VER_NT_SERVER:
                        case VER_NT_DOMAIN_CONTROLLER:
                            tags.insert(L"WindowsServer2012R2"s);
                            break;
                    }
                    break;
            }
            break;
        case 10:
            switch (systemType)
            {
                case VER_NT_WORKSTATION:
                    tags.insert(L"Windows10"s);
                    break;
                case VER_NT_SERVER:
                case VER_NT_DOMAIN_CONTROLLER:
                    tags.insert(L"WindowsServer2016"s);
                    break;
            }
            break;
            break;
        default:
            break;
    }

    switch (g_pDetailsBlock->osvi.wServicePackMajor)
    {
        case 0:
            if (major < 10)
                tags.insert(L"Release#RTM"s);
            break;
        default:
            tags.insert(fmt::format(L"Release#SP{}"sv, g_pDetailsBlock->osvi.wServicePackMajor));
    }

    tags.insert(L"Windows"s);

    std::wstring strProductType;
    if (auto hr = GetSystemType(strProductType); FAILED(hr))
        throw Exception(Severity::Fatal, hr, L"System type not available"sv);
    tags.insert(strProductType);

    tags.insert(fmt::format(L"OSBuild#{}", g_pDetailsBlock->osvi.dwBuildNumber));

    HKEY current_version;
    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", &current_version)
        == ERROR_SUCCESS)
    {
        WCHAR release_id[ORC_MAX_PATH];
        DWORD valueType = 0L;
        DWORD cbData = ORC_MAX_PATH * sizeof(WCHAR);
        if (RegQueryValueExW(current_version, L"DisplayVersion", NULL, &valueType, (LPBYTE)release_id, &cbData)
            == ERROR_SUCCESS)
        {
            if (valueType == REG_SZ)
            {
                tags.insert(fmt::format(L"Release#{}", release_id));
            }
        }
        else if (
            RegQueryValueExW(current_version, L"ReleaseId", NULL, &valueType, (LPBYTE)release_id, &cbData)
            == ERROR_SUCCESS)
        {
            if (valueType == REG_SZ)
            {
                tags.insert(fmt::format(L"Release#{}", release_id));
            }
        }
    }
    return tags;
}

HRESULT Orc::SystemDetails::SetSystemTags(SystemTags tags)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        throw Exception(Severity::Fatal, L"System details information could not be loaded"sv);

    g_pDetailsBlock->Tags.emplace(std::move(tags));
    return S_OK;
}

HRESULT SystemDetails::GetDescriptionString(std::wstring& strDescription)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    strDescription = g_pDetailsBlock->strOSDescription;
    return S_OK;
}

HRESULT SystemDetails::WriteDescriptionString()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
    {
        Log::Debug("Failed Orc::LoadSystemDetails [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(L"System description: {}", g_pDetailsBlock->strOSDescription);
    return S_OK;
}

HRESULT SystemDetails::WriteDescriptionString(ITableOutput& output)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    output.WriteString(g_pDetailsBlock->strOSDescription.c_str());

    return S_OK;
}

HRESULT SystemDetails::GetOSVersion(DWORD& dwMajor, DWORD& dwMinor)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    dwMajor = g_pDetailsBlock->osvi.dwMajorVersion;
    dwMinor = g_pDetailsBlock->osvi.dwMinorVersion;
    return S_OK;
}

std::pair<DWORD, DWORD> Orc::SystemDetails::GetOSVersion()
{
    std::pair<DWORD, DWORD> retval = {0, 0};

    if (SUCCEEDED(GetOSVersion(retval.first, retval.second)))
        return retval;

    throw L"Failed GetOSVersion";
}

Result<std::vector<Orc::SystemDetails::CPUInformation>> Orc::SystemDetails::GetCPUInfo()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    if (auto hr = g_pDetailsBlock->wmi.Initialize())
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return SystemError(hr);
    }

    const auto& wmi = g_pDetailsBlock->wmi;

    auto result = wmi.Query(
        L"SELECT Description,Name,NumberOfCores,NumberOfEnabledCore,NumberOfLogicalProcessors FROM Win32_Processor");

    if (!result)
        return result.error();

    const auto& pEnum = *result;

    std::vector<CPUInformation> retval;

    while (pEnum)
    {
        CComPtr<IWbemClassObject> pclsObj;
        ULONG uReturn = 0;

        pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (0 == uReturn)
            break;

        CPUInformation cpu;

        if (auto descr = WMI::GetProperty<std::wstring>(pclsObj, L"Description"); descr.has_value())
            cpu.Description = *descr;

        if (auto name = WMI::GetProperty<std::wstring>(pclsObj, L"Name"); name.has_value())
            cpu.Name = *name;

        if (auto cores = WMI::GetProperty<ULONG32>(pclsObj, L"NumberOfCores"); cores.has_value())
            cpu.Cores = *cores;

        if (auto cores = WMI::GetProperty<ULONG32>(pclsObj, L"NumberOfEnabledCore"); cores.has_value())
            cpu.EnabledCores = *cores;

        if (auto logical = WMI::GetProperty<ULONG32>(pclsObj, L"NumberOfLogicalProcessors"); logical.has_value())
            cpu.LogicalProcessors = *logical;

        retval.push_back(std::move(cpu));
    }

    return retval;
}

Result<MEMORYSTATUSEX> Orc::SystemDetails::GetPhysicalMemory()
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);

    if (!GlobalMemoryStatusEx(&statex))
        return SystemError(HRESULT_FROM_WIN32(GetLastError()));

    return statex;
}

HRESULT SystemDetails::GetPageSize(DWORD& dwPageSize)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    dwPageSize = g_pDetailsBlock->si.dwPageSize;
    return S_OK;
}

HRESULT SystemDetails::GetLargePageSize(DWORD& dwPageSize)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    dwPageSize = 0L;
    return S_OK;
}

HRESULT SystemDetails::GetArchitecture(WORD& wArch)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    wArch = g_pDetailsBlock->si.wProcessorArchitecture;
    return S_OK;
}

HRESULT SystemDetails::GetComputerName_(std::wstring& strComputerName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    strComputerName = g_pDetailsBlock->strComputerName;
    return S_OK;
}

HRESULT SystemDetails::WriteComputerName()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
    {
        Log::Debug("Failed Orc::LoadSystemDetails [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(L"Computer name: {}", g_pDetailsBlock->strComputerName);

    return S_OK;
}

HRESULT SystemDetails::WriteComputerName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    output.WriteString(g_pDetailsBlock->strComputerName.c_str());

    return S_OK;
}

HRESULT SystemDetails::GetFullComputerName(std::wstring& strComputerName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    strComputerName = g_pDetailsBlock->strFullComputerName;
    return S_OK;
}

HRESULT SystemDetails::WriteFullComputerName()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
    {
        Log::Debug("Failed Orc::LoadSystemDetails [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(L"Full computer name: {}", g_pDetailsBlock->strFullComputerName);
    return S_OK;
}

HRESULT SystemDetails::WriteFullComputerName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    output.WriteString(g_pDetailsBlock->strFullComputerName.c_str());

    return S_OK;
}

HRESULT Orc::SystemDetails::SetOrcComputerName(const std::wstring& strComputerName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    g_pDetailsBlock->strOrcComputerName = strComputerName;

    if (!SetEnvironmentVariableW(OrcComputerName, g_pDetailsBlock->strOrcComputerName.value().c_str()))
        return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

HRESULT SystemDetails::GetOrcComputerName(std::wstring& strComputerName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    strComputerName = g_pDetailsBlock->strOrcComputerName.value_or(g_pDetailsBlock->strComputerName);
    return S_OK;
}

HRESULT SystemDetails::WriteOrcComputerName()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
    {
        Log::Debug("Failed Orc::LoadSystemDetails [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(
        L"Orc computer name: {}", g_pDetailsBlock->strOrcComputerName.value_or(g_pDetailsBlock->strComputerName));

    return S_OK;
}

HRESULT SystemDetails::WriteOrcComputerName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    output.WriteString(g_pDetailsBlock->strOrcComputerName.value_or(g_pDetailsBlock->strComputerName).c_str());

    return S_OK;
}

HRESULT Orc::SystemDetails::SetOrcFullComputerName(const std::wstring& strFullComputerName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    g_pDetailsBlock->strOrcFullComputerName = strFullComputerName;

    if (!SetEnvironmentVariableW(OrcFullComputerName, g_pDetailsBlock->strOrcFullComputerName.value().c_str()))
        return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

HRESULT SystemDetails::GetOrcFullComputerName(std::wstring& strComputerName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;
    strComputerName = g_pDetailsBlock->strOrcFullComputerName.value_or(g_pDetailsBlock->strFullComputerName);
    return S_OK;
}

HRESULT SystemDetails::WriteOrcFullComputerName()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadSystemDetails()))
    {
        Log::Debug("Failed Orc::LoadSystemDetails [{}]", SystemError(hr));
        return hr;
    }

    Log::Debug(
        L"Full Orc computer name: {}",
        g_pDetailsBlock->strOrcFullComputerName.value_or(g_pDetailsBlock->strFullComputerName));

    return S_OK;
}

HRESULT SystemDetails::WriteOrcFullComputerName(ITableOutput& output)
{
    HRESULT hr = LoadSystemDetails();
    if (FAILED(hr))
    {
        return hr;
    }

    output.WriteString(g_pDetailsBlock->strOrcFullComputerName.value_or(g_pDetailsBlock->strFullComputerName).c_str());

    return S_OK;
}

HRESULT SystemDetails::WriteProductType(ITableOutput& output)
{
    HRESULT hr = LoadSystemDetails();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to load system details [{}]", SystemError(hr));
        return hr;
    }

    switch (g_pDetailsBlock->osvi.wProductType)
    {
        case VER_NT_WORKSTATION:
            output.WriteString(L"WorkStation");
            break;
        case VER_NT_SERVER:
            output.WriteString(L"Server");
            break;
        case VER_NT_DOMAIN_CONTROLLER:
            output.WriteString(L"DomainController");
            break;
    }

    return S_OK;
}

HRESULT SystemDetails::GetTimeStamp(std::wstring& strTimeStamp)
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return hr;

    strTimeStamp.assign(g_pDetailsBlock->strTimeStamp);
    return S_OK;
}

HRESULT SystemDetails::GetTimeStampISO8601(std::wstring& strTimeStamp)
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return hr;

    strTimeStamp = ToStringIso8601(g_pDetailsBlock->timestamp);
    return S_OK;
}

Result<Traits::TimeUtc<SYSTEMTIME>> SystemDetails::GetTimeStamp()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
    {
        return SystemError(hr);
    }

    return g_pDetailsBlock->timestamp;
}

HRESULT SystemDetails::WhoAmI(std::wstring& strMe)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    strMe = g_pDetailsBlock->strUserName;

    return S_OK;
}

HRESULT SystemDetails::AmIElevated(bool& bIsElevated)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    bIsElevated = g_pDetailsBlock->bIsElevated;

    return S_OK;
}

HRESULT Orc::SystemDetails::UserSID(std::wstring& strSID)
{
    if (FAILED(LoadSystemDetails()))
        return false;

    strSID = g_pDetailsBlock->strUserSID;

    return S_OK;
}

Result<DWORD> Orc::SystemDetails::GetParentProcessId()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    if (auto hr = g_pDetailsBlock->wmi.Initialize())
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return SystemError(hr);
    }

    const auto& wmi = g_pDetailsBlock->wmi;

    auto query = fmt::format(L"SELECT ParentProcessId FROM Win32_Process WHERE ProcessId = {}", GetCurrentProcessId());

    auto result = wmi.Query(query.c_str());
    if (result.has_error())
        return result.error();

    auto pEnum = *result;
    if (!pEnum)
        return SystemError(HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND));

    CComPtr<IWbemClassObject> pclsObj;
    ULONG uReturn = 0;

    pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
    if (0 == uReturn)
        return SystemError(HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND));

    if (auto parent_id = WMI::GetProperty<ULONG32>(pclsObj, L"ParentProcessId"); parent_id.has_error())
        return SystemError(HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND));
    else
        return *parent_id;
}

Result<std::wstring> Orc::SystemDetails::GetCmdLine()
{
    return std::wstring(GetCommandLineW());
}

Result<std::wstring> Orc::SystemDetails::GetCmdLine(DWORD dwPid)
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    if (auto hr = g_pDetailsBlock->wmi.Initialize())
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return SystemError(hr);
    }

    const auto& wmi = g_pDetailsBlock->wmi;

    if (dwPid == 0)
        return SystemError(E_INVALIDARG);

    // We can now look for the parent command line
    auto query = fmt::format(L"SELECT CommandLine FROM Win32_Process WHERE ProcessId = {}", dwPid);

    auto result = wmi.Query(query.c_str());
    if (!result)
        return result.error();

    const auto& pEnum = result.value();
    if (!pEnum)
        return SystemError(HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND));
    CComPtr<IWbemClassObject> pclsObj;
    ULONG uReturn = 0;

    pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
    if (0 == uReturn)
        return SystemError(HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND));

    if (auto cmdLine = WMI::GetProperty<std::wstring>(pclsObj, L"CommandLine"); cmdLine.has_error())
        return SystemError(HRESULT_FROM_WIN32(ERROR_OBJECT_NOT_FOUND));
    else
        return cmdLine;
}

HRESULT Orc::SystemDetails::GetSystemLocale(std::wstring& strLocale)
{
    wchar_t szName[ORC_MAX_PATH];
    if (GetLocaleInfo(GetSystemDefaultLCID(), LOCALE_SISO639LANGNAME, szName, ORC_MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t szCountry[ORC_MAX_PATH];
    if (GetLocaleInfo(GetSystemDefaultLCID(), LOCALE_SISO3166CTRYNAME, szCountry, ORC_MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    strLocale = fmt::format(L"{}-{}", szName, szCountry);
    return S_OK;
}

HRESULT Orc::SystemDetails::GetUserLocale(std::wstring& strLocale)
{
    wchar_t szName[ORC_MAX_PATH];
    if (GetLocaleInfo(GetUserDefaultLCID(), LOCALE_SISO639LANGNAME, szName, ORC_MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t szCountry[ORC_MAX_PATH];
    if (GetLocaleInfo(GetUserDefaultLCID(), LOCALE_SISO3166CTRYNAME, szCountry, ORC_MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    strLocale = fmt::format(L"{}-{}", szName, szCountry);
    return S_OK;
}

HRESULT Orc::SystemDetails::GetSystemLanguage(std::wstring& strLanguage)
{
    wchar_t szName[ORC_MAX_PATH];
    if (GetLocaleInfo(GetSystemDefaultUILanguage(), LOCALE_SLANGUAGE, szName, ORC_MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    strLanguage.assign(szName);
    return S_OK;
}

HRESULT Orc::SystemDetails::GetUserLanguage(std::wstring& strLanguage)
{
    wchar_t szName[ORC_MAX_PATH];
    if (GetLocaleInfo(GetUserDefaultUILanguage(), LOCALE_SLANGUAGE, szName, ORC_MAX_PATH) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    strLanguage.assign(szName);
    return S_OK;
}

SystemDetails::DriveType SystemDetails::GetPathLocation(const std::wstring& strAnyPath)
{
    WCHAR szVolume[ORC_MAX_PATH];
    if (!GetVolumePathName(strAnyPath.c_str(), szVolume, ORC_MAX_PATH))
    {
        return Drive_Unknown;
    }

    switch (GetDriveTypeW(szVolume))
    {
        case DRIVE_UNKNOWN:
            return Drive_Unknown;
        case DRIVE_NO_ROOT_DIR:
            return Drive_No_Root_Dir;
        case DRIVE_REMOVABLE:
            return Drive_Removable;
        case DRIVE_FIXED:
            return Drive_Fixed;
        case DRIVE_REMOTE:
            return Drive_Remote;
        case DRIVE_CDROM:
            return Drive_CDRom;
        case DRIVE_RAMDISK:
            return Drive_RamDisk;
        default:
            return Drive_Unknown;
    }
}

Result<std::vector<Orc::SystemDetails::PhysicalDrive>> Orc::SystemDetails::GetPhysicalDrives()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    if (auto hr = g_pDetailsBlock->wmi.Initialize())
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return SystemError(hr);
    }

    const auto& wmi = g_pDetailsBlock->wmi;

    auto result = wmi.Query(
        L"SELECT DeviceID,Size,SerialNumber,MediaType,Status,ConfigManagerErrorCode,Availability FROM Win32_DiskDrive");
    if (result.has_error())
        return result.error();

    const auto& pEnum = result.value();

    std::vector<Orc::SystemDetails::PhysicalDrive> retval;

    while (pEnum)
    {
        CComPtr<IWbemClassObject> pclsObj;
        ULONG uReturn = 0;

        pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (0 == uReturn)
            break;

        PhysicalDrive drive;

        if (auto id = WMI::GetProperty<std::wstring>(pclsObj, L"DeviceID"); id.has_error())
            continue;  // If we can't get the deviceId, no need to add it...
        else
            drive.Path = *id;

        if (auto size = WMI::GetProperty<ULONG64>(pclsObj, L"Size"); size.has_value())
            drive.Size = *size;

        if (auto serial = WMI::GetProperty<ULONG32>(pclsObj, L"SerialNumber"); serial.has_value())
            drive.SerialNumber = *serial;

        if (auto type = WMI::GetProperty<std::wstring>(pclsObj, L"MediaType"); type.has_value())
            drive.MediaType = *type;

        if (auto status = WMI::GetProperty<std::wstring>(pclsObj, L"Status"); status.has_value())
            drive.Status = *status;

        if (auto error_code = WMI::GetProperty<ULONG32>(pclsObj, L"ConfigManagerErrorCode"); error_code.has_value())
        {
            if (auto code = *error_code; code != 0)
                drive.ConfigManagerErrorCode = code;
        }
        if (auto availability = WMI::GetProperty<USHORT>(pclsObj, L"Availability"); availability.has_value())
        {
            if (auto avail = *availability; avail != 0)
                drive.Availability = avail;
        }

        retval.push_back(std::move(drive));
    }

    return retval;
}

Result<std::vector<Orc::SystemDetails::MountedVolume>> Orc::SystemDetails::GetMountedVolumes()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    if (auto hr = g_pDetailsBlock->wmi.Initialize())
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return SystemError(hr);
    }

    const auto& wmi = g_pDetailsBlock->wmi;
    auto result = wmi.Query(
        L"SELECT "
        L"Name,FileSystem,Label,DeviceID,DriveType,Capacity,FreeSpace,SerialNumber,BootVolume,SystemVolume,"
        L"LastErrorCode,ErrorDescription FROM Win32_Volume");
    if (result.has_error())
        result.error();

    const auto& pEnum = result.value();

    std::vector<Orc::SystemDetails::MountedVolume> retval;

    while (pEnum)
    {
        CComPtr<IWbemClassObject> pclsObj;
        ULONG uReturn = 0;

        pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (0 == uReturn)
            break;

        MountedVolume volume;

        if (auto name = WMI::GetProperty<std::wstring>(pclsObj, L"Name"); !name)
            continue;  // without name...
        else
            volume.Path = *name;

        if (auto fs = WMI::GetProperty<std::wstring>(pclsObj, L"FileSystem"))
            volume.FileSystem = *fs;

        if (auto label = WMI::GetProperty<std::wstring>(pclsObj, L"Label"))
            volume.Label = *label;

        if (auto device_id = WMI::GetProperty<std::wstring>(pclsObj, L"DeviceID"))
            volume.DeviceId = *device_id;

        if (auto drive_type = WMI::GetProperty<ULONG32>(pclsObj, L"DriveType"))
        {
            switch (drive_type.value())
            {
                case DRIVE_UNKNOWN:
                    volume.Type = Drive_Unknown;
                    break;
                case DRIVE_NO_ROOT_DIR:
                    volume.Type = Drive_No_Root_Dir;
                    break;
                case DRIVE_REMOVABLE:
                    volume.Type = Drive_Removable;
                    break;
                case DRIVE_FIXED:
                    volume.Type = Drive_Fixed;
                    break;
                case DRIVE_REMOTE:
                    volume.Type = Drive_Remote;
                    break;
                case DRIVE_CDROM:
                    volume.Type = Drive_CDRom;
                    break;
                case DRIVE_RAMDISK:
                    volume.Type = Drive_RamDisk;
                    break;
                default:
                    volume.Type = Drive_Unknown;
            }
        }

        if (auto capacity = WMI::GetProperty<ULONG64>(pclsObj, L"Capacity"))
            volume.Size = *capacity;

        if (auto freespace = WMI::GetProperty<ULONG64>(pclsObj, L"FreeSpace"))
            volume.FreeSpace = *freespace;

        if (auto serial = WMI::GetProperty<ULONG32>(pclsObj, L"SerialNumber"))
            volume.SerialNumber = *serial;

        if (auto boot = WMI::GetProperty<bool>(pclsObj, L"BootVolume"))
            volume.bBoot = *boot;

        if (auto system = WMI::GetProperty<bool>(pclsObj, L"SystemVolume"))
            volume.bSystem = *system;

        if (auto errorcode = WMI::GetProperty<ULONG32>(pclsObj, L"LastErrorCode"))
            if (auto code = *errorcode; code != 0LU)
                volume.ErrorCode = code;

        if (auto errordescr = WMI::GetProperty<std::wstring>(pclsObj, L"ErrorDescription"))
            if (auto descr = *errordescr; descr.empty())
                volume.ErrorDesciption = descr;

        retval.push_back(std::move(volume));
    }

    return retval;
}

Result<std::vector<Orc::SystemDetails::QFE>> Orc::SystemDetails::GetOsQFEs()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    if (auto hr = g_pDetailsBlock->wmi.Initialize())
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return SystemError(hr);
    }

    const auto& wmi = g_pDetailsBlock->wmi;
    auto result = wmi.Query(L"SELECT HotFixID,Description,Caption,InstalledOn FROM Win32_QuickFixEngineering");
    if (!result)
        return result.error();

    const auto& pEnum = result.value();

    std::vector<QFE> retval;
    while (pEnum)
    {
        CComPtr<IWbemClassObject> pclsObj;
        ULONG uReturn = 0;

        pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (0 == uReturn)
            break;

        QFE qfe;

        if (auto id = WMI::GetProperty<std::wstring>(pclsObj, L"HotFixID"); !id)
            continue;  // without hotfix id...
        else
            qfe.HotFixId = *id;

        if (auto descr = WMI::GetProperty<std::wstring>(pclsObj, L"Description"))
            qfe.Description = *descr;

        if (auto url = WMI::GetProperty<std::wstring>(pclsObj, L"Caption"))
            qfe.URL = *url;

        if (auto date = WMI::GetProperty<std::wstring>(pclsObj, L"InstalledOn"))
            qfe.InstallDate = *date;

        retval.push_back(std::move(qfe));
    }
    return retval;
}

Result<std::vector<Orc::SystemDetails::EnvVariable>> Orc::SystemDetails::GetEnvironment()
{
    auto env_snap = GetEnvironmentStringsW();

    if (env_snap == NULL)
        return SystemError(HRESULT_FROM_WIN32(GetLastError()));

    BOOST_SCOPE_EXIT(&env_snap) { FreeEnvironmentStringsW(env_snap); }
    BOOST_SCOPE_EXIT_END;

    std::vector<Orc::SystemDetails::EnvVariable> retval;

    auto curVar = (LPWSTR)env_snap;
    while (*curVar)
    {
        auto equals = wcschr(curVar, L'=');
        if (equals && equals != curVar)
        {
            EnvVariable var;
            var.Name.assign(curVar, equals);
            var.Value.assign(equals + 1);
            retval.push_back(std::move(var));
        }
        curVar += lstrlen(curVar) + 1;
    }

    return retval;
}

bool SystemDetails::IsWOW64()
{
    if (FAILED(LoadSystemDetails()))
        return false;
    return g_pDetailsBlock->WOW64;
}

Result<std::vector<Orc::SystemDetails::NetworkAdapter>> Orc::SystemDetails::GetNetworkAdapters()
{
    if (auto hr = LoadSystemDetails(); FAILED(hr))
        return SystemError(hr);

    std::vector<Orc::SystemDetails::NetworkAdapter> retval;

    // Collect network adapters information
    {
        DWORD dwPageSize = 0L;
        SystemDetails::GetPageSize(dwPageSize);

        const DWORD WORKING_BUFFER_SIZE = 4 * dwPageSize;
        Buffer<BYTE> buffer;

        buffer.reserve(WORKING_BUFFER_SIZE);
        ULONG cbRequiredSize = 0L;

        if (auto ret = GetAdaptersAddresses(AF_UNSPEC, 0L, NULL, (PIP_ADAPTER_ADDRESSES)buffer.get(), &cbRequiredSize);
            ret == ERROR_BUFFER_OVERFLOW)
        {
            buffer.reserve(cbRequiredSize);
            if (auto ret =
                    GetAdaptersAddresses(AF_UNSPEC, 0L, NULL, (PIP_ADAPTER_ADDRESSES)buffer.get(), &cbRequiredSize);
                ret != ERROR_SUCCESS)
            {
                return SystemError(HRESULT_FROM_WIN32(ret));
            }

            buffer.resize(cbRequiredSize);
        }
        else if (ret != ERROR_SUCCESS)
        {
            return SystemError(HRESULT_FROM_WIN32(ret));
        }

        // If successful, output some information from the data we received
        auto pCurrAddresses = buffer.get_as<IP_ADAPTER_ADDRESSES>();
        while (pCurrAddresses)
        {
            NetworkAdapter adapter;

            // Adaptor's "cryptic" name (GUID)
            Orc::AnsiToWide(pCurrAddresses->AdapterName, adapter.Name);

            // First, UniCast addresses

            if (auto pUnicast = pCurrAddresses->FirstUnicastAddress; pUnicast != NULL)
            {
                for (; pUnicast != NULL;)
                {
                    if (auto result = GetNetworkAddress(pUnicast->Address); result.has_error())
                        break;
                    else
                    {
                        auto address = *result;
                        address.Mode = AddressMode::UniCast;
                        adapter.Addresses.push_back(std::move(address));
                    }

                    pUnicast = pUnicast->Next;
                }
            }

            if (auto pAnycast = pCurrAddresses->FirstAnycastAddress; pAnycast != NULL)
            {
                for (; pAnycast != NULL;)
                {
                    if (auto result = GetNetworkAddress(pAnycast->Address); result.has_error())
                        break;
                    else
                    {
                        auto address = *result;
                        address.Mode = AddressMode::AnyCast;
                        adapter.Addresses.push_back(std::move(address));
                    }

                    pAnycast = pAnycast->Next;
                }
            }

            if (auto pMulticast = pCurrAddresses->FirstMulticastAddress; pMulticast != NULL)
            {
                for (; pMulticast != NULL;)
                {
                    if (auto result = GetNetworkAddress(pMulticast->Address); result.has_error())
                        break;
                    else
                    {
                        auto address = *result;
                        address.Mode = AddressMode::MultiCast;
                        adapter.Addresses.push_back(std::move(address));
                    }
                    pMulticast = pMulticast->Next;
                }
            }

            auto pDnServer = pCurrAddresses->FirstDnsServerAddress;
            if (pDnServer)
            {
                for (; pDnServer != NULL;)
                {
                    if (auto result = GetNetworkAddress(pDnServer->Address); result.has_error())
                        break;
                    else
                    {
                        auto address = *result;
                        address.Mode = AddressMode::UnknownMode;
                        adapter.DNS.push_back(std::move(address));
                    }

                    pDnServer = pDnServer->Next;
                }
            }

            adapter.DNSSuffix.assign(pCurrAddresses->DnsSuffix);
            adapter.Description.assign(pCurrAddresses->Description);
            adapter.FriendlyName.assign(pCurrAddresses->FriendlyName);

            if (pCurrAddresses->PhysicalAddressLength != 0)
            {
                Buffer<WCHAR, ORC_MAX_PATH> PhysAddress;

                for (auto i = 0; i < (int)pCurrAddresses->PhysicalAddressLength; i++)
                {
                    if (i == (pCurrAddresses->PhysicalAddressLength - 1))
                        fmt::format_to(
                            std::back_insert_iterator(adapter.PhysicalAddress),
                            L"{:0X}",
                            pCurrAddresses->PhysicalAddress[i]);
                    else
                        fmt::format_to(
                            std::back_insert_iterator(adapter.PhysicalAddress),
                            L"{:0X}-",
                            pCurrAddresses->PhysicalAddress[i]);
                }
            }

            retval.push_back(std::move(adapter));

            pCurrAddresses = pCurrAddresses->Next;
        }
    }
    return retval;
}

Result<Orc::SystemDetails::NetworkAddress> Orc::SystemDetails::GetNetworkAddress(SOCKET_ADDRESS& address)
{
    NetworkAddress retval;
    switch (address.lpSockaddr->sa_family)
    {
        case AF_INET:
            retval.Type = AddressType::IPV4;
            break;
        case AF_INET6:
            retval.Type = AddressType::IPV6;
            break;
        default:
            retval.Type = AddressType::IPUnknown;
            break;
    }

    Buffer<WCHAR, ORC_MAX_PATH> ip;
    DWORD dwLength = ORC_MAX_PATH;
    ip.reserve(ORC_MAX_PATH);

    if (WSAAddressToStringW(address.lpSockaddr, address.iSockaddrLength, NULL, ip.get(), &dwLength) == SOCKET_ERROR)
        return SystemError(HRESULT_FROM_WIN32(WSAGetLastError()));

    ip.use(dwLength);

    retval.Address.assign(ip.get());

    return retval;
}

HRESULT SystemDetails::LoadSystemDetails()
{
    if (g_pDetailsBlock)
    {
        return S_OK;
    }

    g_pDetailsBlock = std::make_unique<SystemDetailsBlock>();

    PGNSI pGNSI;
    PGPI pGPI;
    DWORD dwType;

    WCHAR szOSDesc[BUFSIZE];

    ZeroMemory(&g_pDetailsBlock->si, sizeof(SYSTEM_INFO));
    ZeroMemory(&g_pDetailsBlock->osvi, sizeof(OSVERSIONINFOEX));

    g_pDetailsBlock->osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

#pragma warning(suppress : 4996)
    if (!(GetVersionEx((OSVERSIONINFO*)&g_pDetailsBlock->osvi)))
        return 1;

    // Call GetNativeSystemInfo if supported or GetSystemInfo otherwise.

    HMODULE hKernel32 = GetModuleHandle(L"kernel32.dll");

    if (hKernel32 == NULL)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    LPFN_GETLARGEPAGEMINIMUM pGLPM = (LPFN_GETLARGEPAGEMINIMUM)GetProcAddress(hKernel32, "GetLargePageMinimum");
    if (NULL != pGLPM)
        g_pDetailsBlock->dwLargePageSize = pGLPM();
    else
        g_pDetailsBlock->dwLargePageSize = 0L;

    pGNSI = (PGNSI)GetProcAddress(hKernel32, "GetNativeSystemInfo");
    if (NULL != pGNSI)
        pGNSI(&g_pDetailsBlock->si);
    else
        GetSystemInfo(&g_pDetailsBlock->si);

    LPFN_ISWOW64PROCESS fnIsWow64Process;
    fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(hKernel32, "IsWow64Process");

    if (NULL != fnIsWow64Process)
    {
        BOOL Bool;
        if (!fnIsWow64Process(GetCurrentProcess(), &Bool))
        {
            // handle error
        }
        g_pDetailsBlock->WOW64 = Bool ? true : false;
    }

    if (VER_PLATFORM_WIN32_NT != g_pDetailsBlock->osvi.dwPlatformId || g_pDetailsBlock->osvi.dwMajorVersion <= 4)
    {
        return E_FAIL;
    }

    StringCchCopy(szOSDesc, BUFSIZE, TEXT("Microsoft "));

    // Test for the specific product.

    if (g_pDetailsBlock->osvi.dwMajorVersion == 6 || g_pDetailsBlock->osvi.dwMajorVersion == 10)
    {
        if (g_pDetailsBlock->osvi.dwMajorVersion == 10 && g_pDetailsBlock->osvi.dwMinorVersion == 0)
        {
            if (g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION)
                StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows 10 "));
            else
                StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2016 "));
        }
        else if (g_pDetailsBlock->osvi.dwMajorVersion == 6)
        {
            if (g_pDetailsBlock->osvi.dwMinorVersion == 0)
            {
                if (g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Vista "));
                else
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2008 "));
            }
            else if (g_pDetailsBlock->osvi.dwMinorVersion == 1)
            {
                if (g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows 7 "));
                else
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2008 R2 "));
            }
            else if (g_pDetailsBlock->osvi.dwMinorVersion == 2)
            {
                if (g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows 8 "));
                else
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2012 "));
            }
            else if (g_pDetailsBlock->osvi.dwMinorVersion == 3)
            {
                if (g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows 8.1 "));
                else
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2012 R2 "));
            }
        }

        pGPI = (PGPI)GetProcAddress(hKernel32, "GetProductInfo");
        if (pGPI != nullptr)
        {
            pGPI(g_pDetailsBlock->osvi.dwMajorVersion, g_pDetailsBlock->osvi.dwMinorVersion, 0, 0, &dwType);

            switch (dwType)
            {
                case PRODUCT_ULTIMATE:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Ultimate Edition"));
                    break;
                case PRODUCT_PROFESSIONAL:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Professional"));
                    break;
                case PRODUCT_HOME_PREMIUM:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Home Premium Edition"));
                    break;
                case PRODUCT_HOME_BASIC:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Home Basic Edition"));
                    break;
                case PRODUCT_ENTERPRISE:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise Edition"));
                    break;
                case PRODUCT_BUSINESS:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Business Edition"));
                    break;
                case PRODUCT_STARTER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Starter Edition"));
                    break;
                case PRODUCT_CLUSTER_SERVER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Cluster Server Edition"));
                    break;
                case PRODUCT_DATACENTER_SERVER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Datacenter Edition"));
                    break;
                case PRODUCT_DATACENTER_SERVER_CORE:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Datacenter Edition (core installation)"));
                    break;
                case PRODUCT_ENTERPRISE_SERVER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise Edition"));
                    break;
                case PRODUCT_ENTERPRISE_SERVER_CORE:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise Edition (core installation)"));
                    break;
                case PRODUCT_ENTERPRISE_SERVER_IA64:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise Edition for Itanium-based Systems"));
                    break;
                case PRODUCT_SMALLBUSINESS_SERVER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Small Business Server"));
                    break;
                case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Small Business Server Premium Edition"));
                    break;
                case PRODUCT_STANDARD_SERVER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Standard Edition"));
                    break;
                case PRODUCT_STANDARD_SERVER_CORE:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Standard Edition (core installation)"));
                    break;
                case PRODUCT_WEB_SERVER:
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Web Server Edition"));
                    break;
            }
        }
        else
        {
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Edition not available"));
        }
    }

    if (g_pDetailsBlock->osvi.dwMajorVersion == 5 && g_pDetailsBlock->osvi.dwMinorVersion == 2)
    {
        if (GetSystemMetrics(SM_SERVERR2))
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2003 R2, "));
        else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_STORAGE_SERVER)
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Storage Server 2003"));
        else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_WH_SERVER)
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Home Server"));
        else if (
            g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION
            && g_pDetailsBlock->si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        {
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows XP Professional x64 Edition"));
        }
        else
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows Server 2003, "));

        // Test for the server type.
        if (g_pDetailsBlock->osvi.wProductType != VER_NT_WORKSTATION)
        {
            if (g_pDetailsBlock->si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64)
            {
                if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_DATACENTER)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Datacenter Edition for Itanium-based Systems"));
                else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise Edition for Itanium-based Systems"));
            }

            else if (g_pDetailsBlock->si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            {
                if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_DATACENTER)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Datacenter x64 Edition"));
                else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise x64 Edition"));
                else
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Standard x64 Edition"));
            }

            else
            {
                if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_COMPUTE_SERVER)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Compute Cluster Edition"));
                else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_DATACENTER)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Datacenter Edition"));
                else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Enterprise Edition"));
                else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_BLADE)
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Web Edition"));
                else
                    StringCchCat(szOSDesc, BUFSIZE, TEXT("Standard Edition"));
            }
        }
    }

    if (g_pDetailsBlock->osvi.dwMajorVersion == 5 && g_pDetailsBlock->osvi.dwMinorVersion == 1)
    {
        StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows XP "));
        if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_PERSONAL)
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Home Edition"));
        else
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Professional"));
    }

    if (g_pDetailsBlock->osvi.dwMajorVersion == 5 && g_pDetailsBlock->osvi.dwMinorVersion == 0)
    {
        StringCchCat(szOSDesc, BUFSIZE, TEXT("Windows 2000 "));

        if (g_pDetailsBlock->osvi.wProductType == VER_NT_WORKSTATION)
        {
            StringCchCat(szOSDesc, BUFSIZE, TEXT("Professional"));
        }
        else
        {
            if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_DATACENTER)
                StringCchCat(szOSDesc, BUFSIZE, TEXT("Datacenter Server"));
            else if (g_pDetailsBlock->osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                StringCchCat(szOSDesc, BUFSIZE, TEXT("Advanced Server"));
            else
                StringCchCat(szOSDesc, BUFSIZE, TEXT("Server"));
        }
    }

    // Include service pack (if any) and build number.

    if (wcslen(g_pDetailsBlock->osvi.szCSDVersion) > 0)
    {
        StringCchCat(szOSDesc, BUFSIZE, TEXT(" "));
        StringCchCat(szOSDesc, BUFSIZE, g_pDetailsBlock->osvi.szCSDVersion);
    }

    TCHAR buf[80];

    StringCchPrintf(buf, 80, TEXT(" (build %d)"), g_pDetailsBlock->osvi.dwBuildNumber);
    StringCchCat(szOSDesc, BUFSIZE, buf);

    if (g_pDetailsBlock->osvi.dwMajorVersion >= 6)
    {
        if (g_pDetailsBlock->si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            StringCchCat(szOSDesc, BUFSIZE, TEXT(", 64-bit"));
        else if (g_pDetailsBlock->si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
            StringCchCat(szOSDesc, BUFSIZE, TEXT(", 32-bit"));
    }

    g_pDetailsBlock->strOSDescription.assign(szOSDesc);

    WCHAR szComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD dwBufLen = MAX_COMPUTERNAME_LENGTH + 1;
    if (!::GetComputerNameW(szComputerName, &dwBufLen))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    g_pDetailsBlock->strComputerName.assign(szComputerName, dwBufLen);

    WCHAR szFullComputerName[ORC_MAX_PATH];
    DWORD dwFullBufLen = ORC_MAX_PATH;
    if (!::GetComputerNameExW(ComputerNamePhysicalDnsFullyQualified, szFullComputerName, &dwFullBufLen))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    g_pDetailsBlock->strFullComputerName.assign(szFullComputerName, dwFullBufLen);

    WCHAR szOrcComputerName[ORC_MAX_PATH + 1];
    DWORD dwOrcBufLen = ORC_MAX_PATH + 1;

    auto dwOrcComputerName = GetEnvironmentVariableW(OrcComputerName, szOrcComputerName, dwOrcBufLen);
    if (dwOrcComputerName > 0)
        g_pDetailsBlock->strOrcComputerName = std::wstring(szOrcComputerName, dwOrcComputerName);
    else
        g_pDetailsBlock->strOrcComputerName = g_pDetailsBlock->strComputerName;

    WCHAR szOrcFullComputerName[ORC_MAX_PATH];
    DWORD dwOrcFullBufLen = ORC_MAX_PATH;

    auto dwOrcFullComputerName = GetEnvironmentVariableW(OrcFullComputerName, szOrcFullComputerName, dwOrcFullBufLen);
    if (dwOrcFullComputerName > 0)
        g_pDetailsBlock->strOrcFullComputerName = std::wstring(szOrcFullComputerName, dwOrcFullComputerName);
    else
        g_pDetailsBlock->strOrcFullComputerName = g_pDetailsBlock->strFullComputerName;

    HANDLE hToken = INVALID_HANDLE_VALUE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        BOOST_SCOPE_EXIT(&hToken) { CloseHandle(hToken); }
        BOOST_SCOPE_EXIT_END;

        DWORD dwLength = 0L;

        if (!GetTokenInformation(hToken, TokenUser, NULL, 0L, &dwLength))
        {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                return HRESULT_FROM_WIN32(GetLastError());
        }

        CBinaryBuffer userBuf;
        userBuf.SetCount(dwLength);
        ZeroMemory(userBuf.GetData(), dwLength);

        if (GetTokenInformation(hToken, TokenUser, userBuf.GetP<TOKEN_USER>(), dwLength, &dwLength))
        {
            DWORD dwUserNameLen = 0L;
            DWORD dwDomainNameLen = 0L;
            SID_NAME_USE sidType = SidTypeInvalid;

            if (!LookupAccountSidW(
                    NULL, userBuf.Get<TOKEN_USER>(0).User.Sid, NULL, &dwUserNameLen, NULL, &dwDomainNameLen, &sidType))
            {
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                    return HRESULT_FROM_WIN32(GetLastError());
            }

            LPWSTR szSID = nullptr;
            if (!ConvertSidToStringSidW(userBuf.Get<TOKEN_USER>(0).User.Sid, &szSID))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            g_pDetailsBlock->strUserSID = szSID;
            LocalFree(szSID);

            CBinaryBuffer nameBuf, domainBuf;

            nameBuf.SetCount(msl::utilities::SafeInt<USHORT>(dwUserNameLen) * sizeof(WCHAR));
            domainBuf.SetCount(dwDomainNameLen * sizeof(WCHAR));

            if (LookupAccountSidW(
                    NULL,
                    userBuf.Get<TOKEN_USER>(0).User.Sid,
                    nameBuf.GetP<WCHAR>(),
                    &dwUserNameLen,
                    domainBuf.GetP<WCHAR>(),
                    &dwDomainNameLen,
                    &sidType))
            {

                std::wstring strUser;
                strUser.reserve(dwUserNameLen + dwDomainNameLen + 1);

                strUser.assign(domainBuf.GetP<WCHAR>());
                strUser.append(L"\\");
                strUser.append(nameBuf.GetP<WCHAR>());

                std::swap(g_pDetailsBlock->strUserName, strUser);
            }
        }

        TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeDefault;
        dwLength = 0L;
        if (GetTokenInformation(hToken, TokenElevationType, &elevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwLength))
        {
            if (elevationType != TokenElevationTypeLimited)
                g_pDetailsBlock->bIsElevated = true;
            else
                g_pDetailsBlock->bIsElevated = false;
        }
    }

    {
        auto& st = g_pDetailsBlock->timestamp.value;
        GetSystemTime(&st);

        g_pDetailsBlock->strTimeStamp = fmt::format(
            L"{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}"sv, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }
    return S_OK;
}

HRESULT SystemDetails::GetCurrentWorkingDirectory(std::filesystem::path& cwd)
{
    WCHAR path[ORC_MAX_PATH];
    DWORD retval = 0L;
    if (!(retval = GetCurrentDirectory(ORC_MAX_PATH, path)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (retval > ORC_MAX_PATH)
    {
        Buffer<WCHAR> buffer;
        buffer.resize(retval);
        if (!(retval = GetCurrentDirectory(retval, buffer.get())))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        cwd = std::filesystem::path(std::wstring_view(buffer.get(), retval)).lexically_normal();
    }
    else
    {
        cwd = std::filesystem::path(std::wstring_view(path, retval)).lexically_normal();
    }
    return S_OK;
}

HRESULT SystemDetails::GetProcessBinary(std::wstring& strFullPath)
{
    WCHAR szPath[ORC_MAX_PATH];
    if (!GetModuleFileName(NULL, szPath, ORC_MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    strFullPath.assign(szPath);
    return S_OK;
}
