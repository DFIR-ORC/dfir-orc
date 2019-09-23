//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "SystemDetails.h"

#include "LogFileWriter.h"

#include "TableOutput.h"

#include <WinError.h>

#include <boost/scope_exit.hpp>

#include <filesystem>

namespace fs = std::experimental::filesystem;

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
    std::optional<std::wstring> strOrcComputerName;
    std::optional<std::wstring> strOrcFullComputerName;
    std::optional<std::wstring> strProductType;
    std::optional<BYTE> wProductType;
    std::wstring strUserName;
    std::wstring strUserSID;
    std::optional<SystemTags> Tags;
    bool bIsElevated = false;
    DWORD dwLargePageSize = 0L;
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
        throw Exception(Fatal, L"System details information could not be loaded");

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
            throw Exception(Fatal, L"Unsupported architechture {}\r\n", wArch);
    }

    auto [major, minor] = Orc::SystemDetails::GetOSVersion();

    BYTE systemType = 0L;
    if (auto hr = Orc::SystemDetails::GetSystemType(systemType); FAILED(hr))
        throw Exception(Fatal, hr, L"System type not available");

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
            tags.insert(L"RTM"s);
            break;
        case 1:
            tags.insert(L"SP1"s);
            break;
        case 2:
            tags.insert(L"SP2"s);
            break;
        case 3:
            tags.insert(L"SP3"s);
            break;
        case 4:
            tags.insert(L"SP4"s);
            break;
        case 5:
            tags.insert(L"SP5"s);
            break;
        case 6:
            tags.insert(L"SP6"s);
            break;
    }

    std::wstring strProductType;
    if (auto hr = GetSystemType(strProductType); FAILED(hr))
        throw Exception(Fatal, hr, L"System type not available");
    tags.insert(strProductType);

    tags.insert(fmt::format(L"OSBuild#{}", g_pDetailsBlock->osvi.dwBuildNumber));

    HKEY current_version;
    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", &current_version)
        == ERROR_SUCCESS)
    {
        WCHAR release_id[MAX_PATH];
        DWORD valueType = 0L;
        DWORD cbData = MAX_PATH * sizeof(WCHAR);
        if (RegQueryValueExW(current_version, L"ReleaseId", NULL, &valueType, (LPBYTE)release_id, &cbData)
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
        throw Exception(Fatal, L"System details information could not be loaded");

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

HRESULT SystemDetails::WriteDescriptionString(const logger& pLog)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (pLog)
        pLog->WriteString(g_pDetailsBlock->strOSDescription.c_str());

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

    throw L"Failed to retrieve OS Version";
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

HRESULT SystemDetails::WriteComputerName(const logger& pLog)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (pLog)
        pLog->WriteString(g_pDetailsBlock->strComputerName.c_str());

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

HRESULT SystemDetails::WriteFullComputerName(const logger& pLog)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (pLog)
        pLog->WriteString(g_pDetailsBlock->strFullComputerName.c_str());

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

HRESULT SystemDetails::WriteOrcComputerName(const logger& pLog)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (pLog)
        pLog->WriteString(g_pDetailsBlock->strOrcComputerName.value_or(g_pDetailsBlock->strComputerName).c_str());

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

HRESULT SystemDetails::WriteOrcFullComputerName(const logger& pLog)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    if (pLog)
        pLog->WriteString(
            g_pDetailsBlock->strOrcFullComputerName.value_or(g_pDetailsBlock->strFullComputerName).c_str());

    return S_OK;
}

HRESULT SystemDetails::WriteOrcFullComputerName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadSystemDetails()))
        return hr;

    output.WriteString(g_pDetailsBlock->strOrcFullComputerName.value_or(g_pDetailsBlock->strFullComputerName).c_str());

    return S_OK;
}

HRESULT SystemDetails::WriteProductype(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadSystemDetails()))
        return hr;

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
    WCHAR szBuffer[MAX_PATH];

    SYSTEMTIME stNow;
    GetSystemTime(&stNow);

    swprintf_s(
        szBuffer,
        MAX_PATH,
        L"%04d%02d%02d_%02d%02d%02d",
        stNow.wYear,
        stNow.wMonth,
        stNow.wDay,
        stNow.wHour,
        stNow.wMinute,
        stNow.wSecond);

    strTimeStamp.assign(szBuffer);
    return S_OK;
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

SystemDetails::DriveType SystemDetails::GetPathLocation(const std::wstring& strAnyPath)
{
    WCHAR szVolume[MAX_PATH];
    if (!GetVolumePathName(strAnyPath.c_str(), szVolume, MAX_PATH))
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

bool SystemDetails::IsWOW64()
{
    if (FAILED(LoadSystemDetails()))
        return false;
    return g_pDetailsBlock->WOW64;
}

HRESULT SystemDetails::LoadSystemDetails()
{
    if (g_pDetailsBlock)
        return S_OK;

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

    if (VER_PLATFORM_WIN32_NT == g_pDetailsBlock->osvi.dwPlatformId && g_pDetailsBlock->osvi.dwMajorVersion > 4)
    {
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
    }
    else
    {
        return E_FAIL;
    }
    g_pDetailsBlock->strOSDescription.assign(szOSDesc);

    WCHAR szComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD dwBufLen = MAX_COMPUTERNAME_LENGTH + 1;
    if (!::GetComputerNameW(szComputerName, &dwBufLen))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    g_pDetailsBlock->strComputerName.assign(szComputerName, dwBufLen);

    WCHAR szFullComputerName[MAX_PATH];
    DWORD dwFullBufLen = MAX_PATH;
    if (!::GetComputerNameExW(ComputerNamePhysicalDnsFullyQualified, szFullComputerName, &dwFullBufLen))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    g_pDetailsBlock->strFullComputerName.assign(szFullComputerName, dwFullBufLen);

    WCHAR szOrcComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD dwOrcBufLen = MAX_COMPUTERNAME_LENGTH + 1;

    auto dwOrcComputerName = GetEnvironmentVariableW(OrcComputerName, szOrcComputerName, dwOrcBufLen);
    if (dwOrcComputerName > 0)
        g_pDetailsBlock->strOrcComputerName = std::wstring(szOrcComputerName, dwOrcComputerName);
    else
        g_pDetailsBlock->strOrcComputerName = g_pDetailsBlock->strComputerName;

    WCHAR szOrcFullComputerName[MAX_PATH];
    DWORD dwOrcFullBufLen = MAX_PATH;

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
    return S_OK;
}

HRESULT SystemDetails::GetCurrentWorkingDirectory(std::wstring& strCWD)
{
    WCHAR path[MAX_PATH];
    DWORD retval = 0L;
    if (!(retval = GetCurrentDirectory(MAX_PATH, path)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (retval > MAX_PATH)
    {
        CBinaryBuffer buffer;
        buffer.SetCount(retval);
        if (!(retval = GetCurrentDirectory(retval, buffer.GetP<WCHAR>())))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        strCWD.assign(buffer.GetP<WCHAR>(), retval);
    }
    else
    {
        strCWD.assign(path, retval);
    }
    return S_OK;
}

HRESULT SystemDetails::GetProcessBinary(std::wstring& strFullPath)
{
    WCHAR szPath[MAX_PATH];
    if (!GetModuleFileName(NULL, szPath, MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    strFullPath.assign(szPath);
    return S_OK;
}
