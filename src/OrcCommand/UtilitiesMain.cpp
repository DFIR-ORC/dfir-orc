//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "UtilitiesMain.h"

#include <filesystem>
#include <set>

#include <psapi.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/scope_exit.hpp>

#include "ParameterCheck.h"
#include "Configuration/ConfigFile.h"
#include "SystemDetails.h"
#include "NtDllExtension.h"
#include "Kernel32Extension.h"
#include "XmlLiteExtension.h"
#include "WinTrustExtension.h"
#include "VssAPIExtension.h"
#include "EvtLibrary.h"
#include "PSAPIExtension.h"
#include "CaseInsensitive.h"
#include "Utils/WinApi.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command;

void UtilitiesMain::PrintCommonParameters(Orc::Text::Tree& root)
{
    PrintValue(root, "Start time", theStartTime);

    std::wstring computerName;
    SystemDetails::GetComputerName_(computerName);
    PrintValue(root, "Computer name", computerName);

    std::wstring fullComputerName;
    SystemDetails::GetFullComputerName(fullComputerName);
    if (fullComputerName != computerName)
    {
        PrintValue(root, "Full computer name", fullComputerName);
    }

    std::wstring orcComputerName;
    SystemDetails::GetOrcComputerName(orcComputerName);
    if (computerName != orcComputerName)
    {
        PrintValue(root, "DFIR-Orc computer name", orcComputerName);
    }

    std::wstring orcFullComputerName;
    SystemDetails::GetOrcFullComputerName(orcFullComputerName);
    if (orcFullComputerName != fullComputerName && orcFullComputerName != orcComputerName)
    {
        PrintValue(root, "DFIR-Orc computer", orcFullComputerName);
    }

    std::wstring description;
    SystemDetails::GetDescriptionString(description);
    PrintValue(root, "Operating system", description);

    std::wstring userName;
    SystemDetails::WhoAmI(userName);
    bool bIsElevated = false;
    SystemDetails::AmIElevated(bIsElevated);
    PrintValue(root, L"User", fmt::format(L"{}{}", userName, bIsElevated ? L" (elevated)" : L""));

    std::wstring systemType;
    SystemDetails::GetSystemType(systemType);
    PrintValue(root, L"System type", systemType);

    PrintValue(root, L"System tags", boost::join(SystemDetails::GetSystemTags(), ", "));

    std::wstring logFileName(Text::kEmptyW);
    const auto& fileSink = m_logging.fileSink();
    if (fileSink)
    {
        const auto path = fileSink->OutputPath();
        if (path)
        {
            logFileName = *path;
        }
    }

    PrintValue(root, L"Log file", logFileName);
}

void UtilitiesMain::PrintCommonFooter(Orc::Text::Tree& root)
{
    PrintValue(root, "Warning(s)", m_logging.logger().warningCount());
    PrintValue(root, "Error(s)", m_logging.logger().errorCount());
    PrintValue(root, "Critical error(s)", m_logging.logger().criticalCount());

    PrintValue(root, "Finish time", theFinishTime);

    // TODO: std::chrono
    DWORD dwElapsed;
    if (theFinishTickCount < theStartTickCount)
    {
        dwElapsed = (MAXDWORD - theStartTickCount) + theFinishTickCount;
    }
    else
    {
        dwElapsed = theFinishTickCount - theStartTickCount;
    }

    DWORD dwMillisec = dwElapsed % 1000;
    DWORD dwSec = (dwElapsed / 1000) % 60;
    DWORD dwMin = (dwElapsed / (1000 * 60)) % 60;
    DWORD dwHour = (dwElapsed / (1000 * 60 * 60)) % 24;
    DWORD dwDay = (dwElapsed / (1000 * 60 * 60 * 24));

    std::vector<std::wstring> durations;
    if (dwDay)
    {
        durations.push_back(fmt::format(L"{} day(s)", dwDay));
    }

    if (dwHour)
    {
        durations.push_back(fmt::format(L"{} hour(s)", dwHour));
    }

    if (dwMin)
    {
        durations.push_back(fmt::format(L"{} min(s)", dwMin));
    }

    if (dwSec)
    {
        durations.push_back(fmt::format(L"{} sec(s)", dwSec));
    }

    durations.push_back(fmt::format(L"{} msecs", dwMillisec));

    PrintValue(root, "Elapsed time", boost::join(durations, L", "));
}

UtilitiesMain::UtilitiesMain()
    : theStartTime()
    , theFinishTime()
{
    theStartTickCount = 0L;
    theFinishTickCount = 0L;
    m_extensions.reserve(10);
};

void UtilitiesMain::Configure(int argc, const wchar_t* argv[])
{
    UtilitiesLoggerConfiguration::ApplyLogLevel(m_logging, argc, argv);
    UtilitiesLoggerConfiguration::ApplyBacktraceTrigger(m_logging, argc, argv);

    // FIX: Some arguments must be processed very early as others depends
    // on their value. This is not a clean fix but a more global refactor is
    // required on options handling...
    std::wstring computerName;
    std::wstring fullComputerName;
    std::wstring systemType;

    for (int i = 0; i < argc; i++)
    {

        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (ParameterOption(argv[i] + 1, L"Computer", computerName))
                    ;
                else if (ParameterOption(argv[i] + 1, L"FullComputer", fullComputerName))
                    ;
                else if (ParameterOption(argv[i] + 1, L"SystemType", systemType))
                    ;
                break;
            default:
                break;
        }
    }

    if (computerName.empty() && !fullComputerName.empty())
    {
        computerName = fullComputerName;
    }

    if (fullComputerName.empty() && !computerName.empty())
    {
        fullComputerName = computerName;
    }

    if (!computerName.empty())
    {
        SystemDetails::SetOrcComputerName(computerName);
    }

    if (!fullComputerName.empty())
    {
        SystemDetails::SetOrcFullComputerName(fullComputerName);
    }

    if (!systemType.empty())
    {
        SystemDetails::SetSystemType(systemType);
    }
}

bool UtilitiesMain::IsProcessParent(LPCWSTR szImageName)
{
    HRESULT hr = E_FAIL;
    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();

    if (pNtDll == nullptr)
        return false;

    PROCESS_BASIC_INFORMATION pbi;
    ZeroMemory(&pbi, sizeof(PROCESS_BASIC_INFORMATION));
    if (FAILED(pNtDll->NtQueryInformationProcess(
            GetCurrentProcess(), ProcessBasicInformation, &pbi, (ULONG)sizeof(PROCESS_BASIC_INFORMATION), nullptr)))
    {
        Log::Error(L"Failed NtQueryInformationProcess [{}]", LastWin32Error());
        return false;
    }

    DWORD dwParentId = static_cast<DWORD>(reinterpret_cast<DWORD64>(pbi.Reserved3));

    HANDLE hParent = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        dwParentId);  // Reserved3 member contains the parent process ID
    if (hParent == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed OpenProcess (parent process pid: {}, [{}])", dwParentId, SystemError(hr));
        return false;
    }

    BOOST_SCOPE_EXIT(&hParent)
    {
        if (hParent != NULL)
            CloseHandle(hParent);
        hParent = NULL;
    }
    BOOST_SCOPE_EXIT_END;

    WCHAR szImageFileName[MAX_PATH];
    if (!GetProcessImageFileName(hParent, szImageFileName, MAX_PATH))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed GetProcessImageFileName [{}]", SystemError(hr));
        return false;
    }

    Log::Debug(L"Parent process file name is '{}'", szImageFileName);

    WCHAR szImageBaseName[MAX_PATH];
    if (FAILED(hr = GetFileNameForFile(szImageFileName, szImageBaseName, MAX_PATH)))
    {
        Log::Error(L"Failed to build filename for '{}'", szImageFileName);
        return false;
    }

    if (_wcsicmp(szImageBaseName, szImageName))
        return false;

    return true;
}

HRESULT Orc::Command::UtilitiesMain::LoadCommonExtensions()
{
    auto ntdll = ExtensionLibrary::GetLibrary<NtDllExtension>();
    if (!ntdll)
    {
        Log::Error(L"Failed to initialize ntdll extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(ntdll));

    auto kernel32 = ExtensionLibrary::GetLibrary<Kernel32Extension>();
    if (!kernel32)
    {
        Log::Error(L"Failed to initialize kernel extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(kernel32));

    auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();
    if (!xmllite)
    {
        Log::Error(L"Failed to initialize xmllite extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(xmllite));

    return S_OK;
}

HRESULT Orc::Command::UtilitiesMain::LoadWinTrust()
{
    auto wintrust = ExtensionLibrary::GetLibrary<WinTrustExtension>();
    if (!wintrust)
    {
        Log::Error(L"Failed to initialize wintrust extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(wintrust));
    return S_OK;
}

HRESULT Orc::Command::UtilitiesMain::LoadEvtLibrary()
{
    auto wevtapi = ExtensionLibrary::GetLibrary<EvtLibrary>();
    if (!wevtapi)
    {
        Log::Error(L"Failed to initialize wevtapi extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(wevtapi));
    return S_OK;
}

HRESULT Orc::Command::UtilitiesMain::LoadPSAPI()
{
    auto psapi = ExtensionLibrary::GetLibrary<PSAPIExtension>();
    if (!psapi)
    {
        Log::Error(L"Failed to initialize psapi extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(psapi));
    return S_OK;
}

bool UtilitiesMain::OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec::Kind supportedTypes, OutputSpec& anOutput)
{
    HRESULT hr = E_FAIL;

    const auto cchOption = wcslen(szOption);
    if (_wcsnicmp(szArg, szOption, cchOption))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{0} should be like: /{0}=c:\\temp", szOption);
        return false;
    }
    if (pEquals != szArg + cchOption)
    {
        // Argument 'szArgs' starts with 'szOption' but it is longer
        return false;
    }

    if (FAILED(hr = anOutput.Configure(supportedTypes, pEquals + 1)))
    {
        Log::Error(L"An error occurred when evaluating output for option /{}={}", szOption, pEquals + 1);
        return false;
    }
    if (hr == S_FALSE)
    {
        Log::Error(L"None of the supported output for option /{} matched {}", szOption, szArg);
        return false;
    }
    return true;
}

bool UtilitiesMain::OutputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{0} should be like: /{0}=c:\\temp\\OutputFile.csv", szOption);
        return false;
    }
    if (auto hr = GetOutputFile(pEquals + 1, strOutputFile, true); FAILED(hr))
    {
        Log::Error(L"Invalid output dir specified: {}", pEquals + 1);
        return false;
    }
    return true;
}

bool UtilitiesMain::OutputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputDir)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{0} should be like: /{0}=c:\\temp", szOption);
        return false;
    }
    if (auto hr = GetOutputDir(pEquals + 1, strOutputDir, true); FAILED(hr))
    {
        Log::Error(L"Invalid output dir specified: {}", pEquals + 1);
        return false;
    }
    return true;
}

bool UtilitiesMain::InputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strInputFile)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{0} should be like: /{0}=c:\\temp\\InputFile.csv", szOption);
        return false;
    }

    if (auto hr = ExpandFilePath(pEquals + 1, strInputFile); FAILED(hr))
    {
        Log::Error(L"Invalid input file specified: {}", pEquals + 1);
        return false;
    }
    return true;
}

bool UtilitiesMain::InputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strInputDir)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{0} should be like: /{0}=c:\\temp\\InputDirectory", szOption);
        return false;
    }

    if (auto hr = ExpandDirectoryPath(pEquals + 1, strInputDir); FAILED(hr))
    {
        Log::Error(L"Invalid input directory specified: {}", pEquals + 1);
        return false;
    }
    return true;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
        return false;
    }
    strParameter = pEquals + 1;
    return true;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
        return false;
    }
    HRESULT hr = E_FAIL;
    LARGE_INTEGER li;
    if (FAILED(hr = GetIntegerFromArg(pEquals + 1, li)))
    {
        if (FAILED(hr = GetIntegerFromHexaString(pEquals + 1, li)))
        {
            return false;
        }
    }
    ullParameter = li.QuadPart;
    return true;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
        return false;
    }
    HRESULT hr = E_FAIL;
    LARGE_INTEGER li;
    if (FAILED(hr = GetIntegerFromArg(pEquals + 1, li)))
    {
        if (FAILED(hr = GetIntegerFromHexaString(pEquals + 1, li)))
        {
            return false;
        }
    }
    if (li.QuadPart > MAXDWORD)
    {
        Log::Error("Parameter is too big (>MAXDWORD)");
    }
    dwParameter = li.LowPart;
    return true;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::minutes& minParameter)
{
    DWORD dwValue = 0;
    if (ParameterOption(szArg, szOption, dwValue))
    {
        minParameter = std::chrono::minutes(dwValue);
        return true;
    }
    return false;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::seconds& sParameter)
{
    DWORD dwValue = 0;
    if (ParameterOption(szArg, szOption, dwValue))
    {
        sParameter = std::chrono::seconds(dwValue);
        return true;
    }
    return false;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::milliseconds& msParameter)
{
    DWORD dwValue = 0;
    if (ParameterOption(szArg, szOption, dwValue))
    {
        msParameter = std::chrono::milliseconds(dwValue);
        return true;
    }
    return false;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=<yes>|<no>", szOption, szOption);
        return false;
    }

    std::wstring param = pEquals + 1;

    if (!param.compare(L"no"))
    {
        bParameter = false;
    }
    else
    {
        bParameter = true;
    }
    return true;
}

bool UtilitiesMain::OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        strParameter.clear();
    }
    else
    {
        strParameter = pEquals + 1;
    }
    return true;
}

bool UtilitiesMain::OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        ullParameter = 0LL;
    }
    else
    {
        HRESULT hr = E_FAIL;
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(pEquals + 1, li)))
        {
            if (FAILED(hr = GetIntegerFromHexaString(pEquals + 1, li)))
            {
                return false;
            }
        }
        ullParameter = li.QuadPart;
    }
    return true;
}

bool UtilitiesMain::OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;
    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        dwParameter = 0L;
    }
    else
    {
        HRESULT hr = E_FAIL;
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(pEquals + 1, li)))
        {
            if (FAILED(hr = GetIntegerFromHexaString(pEquals + 1, li)))
            {
                return false;
            }
        }
        if (li.QuadPart > MAXDWORD)
        {
            Log::Error("Parameter is too big (>MAXDWORD)");
        }
        dwParameter = li.LowPart;
    }
    return true;
}

bool UtilitiesMain::FileSizeOption(LPCWSTR szArg, LPCWSTR szOption, DWORDLONG& dwlFileSize)
{
    HRESULT hr = E_FAIL;

    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=32M", szArg, szOption);
        return false;
    }
    LARGE_INTEGER liSize;
    liSize.QuadPart = 0;
    if (FAILED(hr = GetFileSizeFromArg(pEquals + 1, liSize)))
    {
        Log::Error(L"Option /{} Failed to convert into a size", szArg);
        return false;
    }
    dwlFileSize = liSize.QuadPart;
    return true;
}

bool UtilitiesMain::AltitudeOption(LPCWSTR szArg, LPCWSTR szOption, LocationSet::Altitude& altitude)
{
    HRESULT hr = E_FAIL;

    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=highest|lowest|exact", szArg, szOption);
        return false;
    }
    altitude = LocationSet::GetAltitudeFromString(pEquals + 1);
    return true;
}

bool UtilitiesMain::BooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bPresent)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;
    bPresent = true;
    return true;
}

bool UtilitiesMain::BooleanOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bPresent)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;
    bPresent = true;
    return true;
}

bool UtilitiesMain::BooleanExactOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bPresent)
{
    if (wcslen(szArg) != wcslen(szOption))
    {
        return false;
    }

    return BooleanOption(szArg, szOption, bPresent);
}

bool UtilitiesMain::ToggleBooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bPresent)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    bPresent = !bPresent;
    return true;
}

bool UtilitiesMain::ShadowsOption(
    LPCWSTR szArg,
    LPCWSTR szOption,
    boost::logic::tribool& bAddShadows,
    std::optional<LocationSet::ShadowFilters>& filters)
{
    if (BooleanExactOption(szArg, szOption, bAddShadows))
    {
        return true;
    }

    std::wstring shadows;
    if (!ParameterOption(szArg, szOption, shadows))
    {
        return false;
    }

    ParseShadowOption(shadows, bAddShadows, filters);
    return true;
}

void UtilitiesMain::ParseShadowOption(
    const std::wstring& shadows,
    boost::logic::tribool& bAddShadows,
    std::optional<LocationSet::ShadowFilters>& filters)
{
    if (shadows.empty())
    {
        return;
    }

    if (shadows == L"no"sv)
    {
        bAddShadows = false;
        return;
    }

    bAddShadows = true;
    if (shadows != L"yes"sv)
    {
        filters = LocationSet::ShadowFilters();
        boost::split(*filters, shadows, boost::is_any_of(L",;|"));
    }
}

bool UtilitiesMain::LocationExcludeOption(
    LPCWSTR szArg,
    LPCWSTR szOption,
    std::optional<LocationSet::PathExcludes>& excludes)
{
    std::wstring value;
    if (!ParameterOption(szArg, szOption, value))
    {
        return false;
    }

    if (value.empty())
    {
        excludes = {};
        return true;
    }

    ParseLocationExcludes(value, excludes);
    return true;
}

void UtilitiesMain::ParseLocationExcludes(
    const std::wstring& rawExcludes,
    std::optional<LocationSet::PathExcludes>& excludes)
{
    if (rawExcludes.empty())
    {
        return;
    }

    std::vector<std::wstring> splits;
    boost::split(splits, rawExcludes, boost::is_any_of(L",;|"));

    for (auto& exclude : splits)
    {
        std::error_code ec;
        exclude = ExpandEnvironmentStringsApi(exclude.c_str(), ec);
        if (ec)
        {
            Log::Debug(L"Failed to expand environment variable for excluded path: '{}' [{}]", exclude, ec);
            return;
        }

        if (exclude.size() == wstring_view(L"C:\\").size() && exclude[2] == L'/')
        {
            exclude[2] = L'\\';
        }

        if (exclude.size() == wstring_view(L"C:").size() && exclude[1] == L':')
        {
            exclude.push_back(L'\\');
        }
    }

    excludes = LocationSet::PathExcludes();
    excludes->insert(std::make_move_iterator(std::begin(splits)), std::make_move_iterator(std::end(splits)));
}

bool UtilitiesMain::CryptoHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, CryptoHashStream::Algorithm& algo)
{
    HRESULT hr = E_FAIL;

    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=hash_functions", szArg, szOption);
        return false;
    }

    std::set<wstring> keys;
    std::wstring values = pEquals + 1;
    boost::split(keys, values, boost::is_any_of(L","));

    algo = CryptoHashStream::Algorithm::Undefined;

    for (const auto& key : keys)
    {
        auto one = CryptoHashStream::GetSupportedAlgorithm(key.c_str());
        if (one == CryptoHashStream::Algorithm::Undefined)
        {
            Log::Warn(L"Hash algorithm '{}' is not supported", key);
        }
        else
        {
            algo |= one;
        }
    }

    return true;
}

bool UtilitiesMain::FuzzyHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, FuzzyHashStream::Algorithm& algo)
{
    HRESULT hr = E_FAIL;

    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        Log::Error(L"Option /{} should be like: /{}=32M", szArg, szOption);
        return false;
    }

    std::set<wstring> keys;
    std::wstring values = pEquals + 1;
    boost::split(keys, values, boost::is_any_of(L","));

    for (const auto& key : keys)
    {
        algo |= FuzzyHashStream::GetSupportedAlgorithm(key.c_str());
    }
    return true;
}

bool UtilitiesMain::EncodingOption(LPCWSTR szArg, OutputSpec::Encoding& anEncoding)
{
    if (!_wcsnicmp(szArg, L"utf8", wcslen(L"utf8")))
    {
        anEncoding = OutputSpec::Encoding::UTF8;
        return true;
    }
    else if (!_wcsnicmp(szArg, L"utf16", wcslen(L"utf16")))
    {
        anEncoding = OutputSpec::Encoding::UTF16;
        return true;
    }
    return false;
}

bool UtilitiesMain::ProcessPriorityOption(LPCWSTR szArg, LPCWSTR szOption)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    return true;
}

bool UtilitiesMain::WaitForDebugger(int argc, const WCHAR* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (WaitForDebuggerOption(argv[i] + 1))
                    return true;
        }
    }
    return false;
}

bool UtilitiesMain::WaitForDebuggerOption(LPCWSTR szArg)
{
    using namespace std::chrono_literals;
    if (_wcsnicmp(szArg, L"WaitForDebugger", wcslen(L"WaitForDebugger")))
        return false;
    Log::Info("Waiting 30 seconds for a debugger to attach...");
    auto counter = 0LU;
    while (!IsDebuggerPresent() && counter < 60)
    {
        counter++;
        Sleep(500);
    }
    if (counter < 60)
        Log::Info("Debugger connected!");
    else
        Log::Info("No debugger connected... let's continue");
    return true;
}

bool UtilitiesMain::IgnoreWaitForDebuggerOption(LPCWSTR szArg)
{
    using namespace std::chrono_literals;
    if (!_wcsnicmp(szArg, L"WaitForDebugger", wcslen(L"WaitForDebugger")))
        return true;
    return false;
}

bool UtilitiesMain::IgnoreLoggingOptions(LPCWSTR szArg)
{
    if (!_wcsnicmp(szArg, L"Verbose", wcslen(L"Verbose")) || !_wcsnicmp(szArg, L"Trace", wcslen(L"Trace"))
        || !_wcsnicmp(szArg, L"Debug", wcslen(L"Debug")) || !_wcsnicmp(szArg, L"Info", wcslen(L"Info"))
        || !_wcsnicmp(szArg, L"Warn", wcslen(L"Warn")) || !_wcsnicmp(szArg, L"Error", wcslen(L"Error"))
        || !_wcsnicmp(szArg, L"Critical", wcslen(L"Critical")) || !_wcsnicmp(szArg, L"LogFile", wcslen(L"LogFile"))
        || !_wcsnicmp(szArg, L"NoConsole", wcslen(L"NoConsole")) || !_wcsnicmp(szArg, L"Log", wcslen(L"Log")))
        return true;
    return false;
}

bool UtilitiesMain::IgnoreConfigOptions(LPCWSTR szArg)
{
    if (!_wcsnicmp(szArg, L"Config", wcslen(L"Config")) || !_wcsnicmp(szArg, L"Schema", wcslen(L"Schema"))
        || !_wcsnicmp(szArg, L"Local", wcslen(L"Local")))
        return true;
    return false;
}

bool UtilitiesMain::IgnoreEarlyOptions(LPCWSTR szArg)
{
    const std::vector<std::wstring_view> kIgnoredList = {L"computer", L"fullcomputer", L"systemtype"};

    std::wstring arg(szArg);

    const auto pos = arg.find_first_of(L'=');
    if (pos != std::wstring::npos)
    {
        arg.resize(pos);
    }

    boost::algorithm::to_lower(arg);

    for (const auto& ignored : kIgnoredList)
    {
        if (ignored == arg)
        {
            return true;
        }
    }

    return false;
}

bool UtilitiesMain::IgnoreCommonOptions(LPCWSTR szArg)
{
    if (IgnoreWaitForDebuggerOption(szArg))
    {
        return true;
    }

    if (IgnoreConfigOptions(szArg))
    {
        return true;
    }

    if (IgnoreLoggingOptions(szArg))
    {
        return true;
    }

    if (IgnoreEarlyOptions(szArg))
    {
        return true;
    }

    return false;
}

bool UtilitiesMain::UsageOption(LPCWSTR szArg)
{
    if (!_wcsnicmp(szArg, L"Help", wcslen(szArg)) || !_wcsnicmp(szArg, L"?", wcslen(szArg))
        || !_wcsnicmp(szArg, L"h", wcslen(szArg)))
    {
        PrintUsage();
        exit(0);
    }
    return false;
}

void UtilitiesMain::PrintHeader(LPCWSTR szToolName, LPCWSTR szToolDescription, LPCWSTR szVersion)
{
    if (szToolName)
    {
        m_console.Write("{} {}", szToolName, szVersion);

        const std::wstring metaName(kOrcMetaNameW);
        const std::wstring metaVersion(kOrcMetaVersionW);
        if (!metaName.empty() && !metaVersion.empty())
        {
            m_console.Write(" ({} {})", metaName, metaVersion);
        }

        m_console.PrintNewLine();
        m_console.PrintNewLine();
    }

    if (szToolDescription)
    {
        m_console.Print("{}", szToolDescription);
        m_console.PrintNewLine();
    }
}
