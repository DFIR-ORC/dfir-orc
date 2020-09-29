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

#include <Psapi.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/scope_exit.hpp>

#include "ParameterCheck.h"
#include "ConfigFile.h"
#include "SystemDetails.h"
#include "NtDllExtension.h"
#include "Kernel32Extension.h"
#include "XmlLiteExtension.h"
#include "WinTrustExtension.h"
#include "VssAPIExtension.h"
#include "EvtLibrary.h"
#include "PSAPIExtension.h"
#include "CaseInsensitive.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command;

UtilitiesMain::UtilitiesMain()
{
    ZeroMemory(&theStartTime, sizeof(SYSTEMTIME));
    ZeroMemory(&theFinishTime, sizeof(SYSTEMTIME));
    theStartTickCount = 0L;
    theFinishTickCount = 0L;
    m_extensions.reserve(10);
};

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
        spdlog::error(L"Failed NtQueryInformationProcess (code: {:#x})", GetLastError());
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
        spdlog::error("Failed OpenProcess (parent process pid: {}, code: {:#x})", dwParentId, hr);
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
        spdlog::error("Failed GetProcessImageFileName (code {:#x})", hr);
        return false;
    }

    spdlog::debug(L"Parent process file name is '{}'", szImageFileName);

    WCHAR szImageBaseName[MAX_PATH];
    if (FAILED(hr = GetFileNameForFile(szImageFileName, szImageBaseName, MAX_PATH)))
    {
        spdlog::error(L"Failed to build filename for '{}'", szImageFileName);
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
        spdlog::error(L"Failed to initialize ntdll extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(ntdll));

    auto kernel32 = ExtensionLibrary::GetLibrary<Kernel32Extension>();
    if (!kernel32)
    {
        spdlog::error(L"Failed to initialize kernel extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(kernel32));

    auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();
    if (!xmllite)
    {
        spdlog::error(L"Failed to initialize xmllite extension");
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
        spdlog::error(L"Failed to initialize wintrust extension");
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
        spdlog::error(L"Failed to initialize wevtapi extension");
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
        spdlog::error(L"Failed to initialize psapi extension");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(psapi));
    return S_OK;
}

HRESULT UtilitiesMain::PrintSystemType()
{
    wstring strSystemType;
    SystemDetails::GetSystemType(strSystemType);

    spdlog::info(L"System type: {}", strSystemType);
    return S_OK;
}

HRESULT UtilitiesMain::PrintSystemTags()
{
    const auto& tags = SystemDetails::GetSystemTags();

    std::wstring strTags;
    bool bFirst = true;
    for (const auto& tag : tags)
    {
        if (!bFirst)
            strTags.append(L",");
        strTags.append(tag);
        bFirst = false;
    }

    spdlog::info(L"System tags: {}", strTags);
    return S_OK;
}

HRESULT UtilitiesMain::PrintComputerName()
{
    wstring strComputerName;
    SystemDetails::GetComputerName_(strComputerName);
    m_console.Print(L"{:>32}: {}", "Computer", strComputerName);

    wstring strFullComputerName;
    SystemDetails::GetFullComputerName(strFullComputerName);

    if (strFullComputerName != strComputerName)
    {
        m_console.Print(L"{:>32}: {}", "Full computer", strFullComputerName);
    }

    wstring strOrcComputerName;
    SystemDetails::GetOrcComputerName(strOrcComputerName);
    if (strComputerName != strOrcComputerName)
    {
        m_console.Print(L"{:>32}: {}", "DFIR-Orc Computer", strOrcComputerName);
    }

    wstring strOrcFullComputerName;
    SystemDetails::GetOrcFullComputerName(strOrcFullComputerName);

    if (strOrcFullComputerName != strFullComputerName && strOrcFullComputerName != strOrcComputerName)
    {
        m_console.Print(L"{:>32}: {}", "DFIR-Orc Computer", strOrcFullComputerName);
    }

    return S_OK;
}

HRESULT UtilitiesMain::PrintWhoAmI()
{
    wstring strUserName;
    SystemDetails::WhoAmI(strUserName);
    bool bIsElevated = false;
    SystemDetails::AmIElevated(bIsElevated);
    spdlog::info(L"User                  : {}{}", strUserName, bIsElevated ? L" (elevated)" : L"");
    return S_OK;
}

HRESULT UtilitiesMain::PrintOperatingSystem()
{
    wstring strDesc;
    SystemDetails::GetDescriptionString(strDesc);
    spdlog::info(L"Operating system      : {}", strDesc);
    return S_OK;
}

HRESULT UtilitiesMain::PrintExecutionTime()
{
    GetSystemTime(&theFinishTime);
    theFinishTickCount = GetTickCount();
    DWORD dwElapsed;

    m_console.Print("Finish time: {}", theFinishTime);

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

    m_console.Print(L"Elapsed time: {}", boost::join(durations, L", "));
    return S_OK;
}

LPCWSTR UtilitiesMain::GetEncoding(OutputSpec::Encoding anEncoding)
{
    switch (anEncoding)
    {
        case OutputSpec::Encoding::UTF16:
            return L" (encoding=UTF16)";
        case OutputSpec::Encoding::UTF8:
            return L" (encoding=UTF8)";
        default:
            return L" (unkown encoding)";
    }
}

LPCWSTR UtilitiesMain::GetCompression(const std::wstring& strCompression)
{
    if (strCompression.empty())
        return L"";
    static WCHAR szCompression[MAX_PATH];
    swprintf_s(szCompression, MAX_PATH, L" (compression %s)", strCompression);
    return szCompression;
}

HRESULT UtilitiesMain::PrintOutputOption(LPCWSTR szOutputName, const OutputSpec& anOutput)
{
    if (anOutput.Path.empty() && anOutput.Type != OutputSpec::Kind::SQL)
    {
        spdlog::info(L"{}: Empty", szOutputName);
        return S_OK;
    }

    switch (anOutput.Type)
    {
        case OutputSpec::Kind::Directory:
            spdlog::info(L"{} directory    : {}{}", szOutputName, anOutput.Path, GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::Archive:
            spdlog::info(
                L"{} archive      : {}{}{}",
                szOutputName,
                anOutput.Path,
                GetEncoding(anOutput.OutputEncoding),
                GetCompression(anOutput.Compression));
            break;
        case OutputSpec::Kind::TableFile:
            spdlog::info(
                L"{} table        : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::CSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV:
            spdlog::info(
                L"{}{} CSV          : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::TSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV:
            spdlog::info(
                L"{} TSV          : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::Parquet:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet:
            spdlog::info(
                L"{} Parquet      : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::ORC:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC:
            spdlog::info(
                L"{} Parquet      : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::StructuredFile:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON:
            spdlog::info(
                L"{} structured   : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::File:
            spdlog::info(
                L"{} file         : {}{}", szOutputName, anOutput.Path.c_str(), GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::SQL:
            spdlog::info(
                L"{} SQLServer  : {} (table: {})", szOutputName, anOutput.ConnectionString, anOutput.TableName);
            break;
        case OutputSpec::Kind::None:
            spdlog::info(L"{} file         : None", szOutputName);
            break;
        default:
            spdlog::info(L"{}              : Unsupported", szOutputName);
            break;
    }

    if (anOutput.UploadOutput != nullptr)
    {
        spdlog::info(L"Upload configuration:", szOutputName);
        LPWSTR szMethod = L"None";
        switch (anOutput.UploadOutput->Method)
        {
            case OutputSpec::UploadMethod::FileCopy:
                szMethod = L"File copy";
                break;
            case OutputSpec::UploadMethod::BITS:
                szMethod = L"Background Intelligent Transfer Service (BITS)";
                break;
            case OutputSpec::UploadMethod::NoUpload:
                szMethod = L"No upload";
                break;
        }
        spdlog::info(L"   --> Method         : {}", szMethod);

        LPWSTR szOperation = L"NoOp";
        switch (anOutput.UploadOutput->Operation)
        {
            case OutputSpec::UploadOperation::Copy:
                szOperation = L"Copy file to upload location";
                break;
            case OutputSpec::UploadOperation::Move:
                szOperation = L"Move file to upload location";
                break;
            case OutputSpec::UploadOperation::NoOp:
                break;
        }
        spdlog::info(L"   --> Operation      : {}", szOperation);

        LPWSTR szMode = L"NoMode";
        switch (anOutput.UploadOutput->Mode)
        {
            case OutputSpec::UploadMode::Asynchronous:
                szMode = L"Asynchronous";
                break;
            case OutputSpec::UploadMode::Synchronous:
                szMode = L"Synchronous";
                break;
        }
        spdlog::info(L"   --> Mode           : {}", szMode);

        if (!anOutput.UploadOutput->JobName.empty())
        {
            spdlog::info(L"   --> Job name       : {}", anOutput.UploadOutput->JobName);
        }
        spdlog::info(
            L"   --> Server name    : {}, path = {}",
            anOutput.UploadOutput->ServerName.c_str(),
            anOutput.UploadOutput->RootPath.c_str());
        if (!anOutput.UploadOutput->UserName.empty())
        {
            spdlog::info(L"   --> User name      : {}", anOutput.UploadOutput->UserName);
        }
        LPWSTR szAuthScheme = L"NoAuth";
        switch (anOutput.UploadOutput->AuthScheme)
        {
            case OutputSpec::UploadAuthScheme::Anonymous:
                szAuthScheme = L"Anonymous";
                break;
            case OutputSpec::UploadAuthScheme::Basic:
                szAuthScheme = L"Basic";
                break;
            case OutputSpec::UploadAuthScheme::NTLM:
                szAuthScheme = L"NTLM";
                break;
            case OutputSpec::UploadAuthScheme::Kerberos:
                szAuthScheme = L"Kerberos";
                break;
            case OutputSpec::UploadAuthScheme::Negotiate:
                szAuthScheme = L"Negotiate";
                break;
        }
        spdlog::info(L"   --> Auth Scheme    : {}", szAuthScheme);
    }

    return S_OK;
}

HRESULT UtilitiesMain::PrintBooleanOption(LPCWSTR szOptionName, bool bValue)
{
    if (bValue)
    {
        spdlog::info(L"{}     : On", szOptionName);
    }
    else
    {
        spdlog::info(L"{}     : Off", szOptionName);
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintBooleanOption(LPCWSTR szOptionName, const boost::logic::tribool& bValue)
{
    if (bValue)
    {
        spdlog::info(L"{}    : On", szOptionName);
    }
    else if (!bValue)
    {
        spdlog::info(L"{}    : Off", szOptionName);
    }
    else
    {
        spdlog::info(L"{}    : Indeterminate", szOptionName);
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintIntegerOption(LPCWSTR szOptionName, DWORD dwOption)
{
    spdlog::info(L"{}     : {}", szOptionName, dwOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintIntegerOption(LPCWSTR szOptionName, LONGLONG llOption)
{
    spdlog::info(L"{}     : {}", szOptionName, llOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintIntegerOption(LPCWSTR szOptionName, ULONGLONG ullOption)
{
    spdlog::info(L"{}     : {}", szOptionName, ullOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintHashAlgorithmOption(LPCWSTR szOptionName, CryptoHashStream::Algorithm algs)
{
    if (algs == CryptoHashStream::Algorithm::Undefined)
    {
        spdlog::info(L"{}     : None", szOptionName);
    }
    else
    {
        spdlog::info(L"{}     : {}", szOptionName, CryptoHashStream::GetSupportedAlgorithm(algs));
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintHashAlgorithmOption(LPCWSTR szOptionName, FuzzyHashStream::Algorithm algs)
{
    if (algs == FuzzyHashStream::Algorithm::Undefined)
    {
        spdlog::info(L"{}     : None", szOptionName);
    }
    else
    {
        spdlog::info(L"{}     : {}", szOptionName, FuzzyHashStream::GetSupportedAlgorithm(algs).c_str());
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintStringOption(LPCWSTR szOptionName, LPCWSTR szOption)
{
    spdlog::info(L"{}     : %s", szOptionName, szOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintFormatedStringOption(LPCWSTR szOptionName, LPCWSTR szFormat, va_list argList)
{
    spdlog::info(L"{}     : ", szOptionName);
    // TODO: _L_->WriteFormatedString(szFormat, argList);
    return S_OK;
}

HRESULT UtilitiesMain::PrintFormatedStringOption(LPCWSTR szOptionName, LPCWSTR szFormat, ...)
{
    spdlog::info(L"{}     : ", szOptionName);
    va_list argList;
    va_start(argList, szFormat);
    // TODO: _L_->WriteFormatedString(szFormat, argList);
    va_end(argList);
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
        spdlog::error(L"Option /{} should be like: /{}=c:\\temp", szOption, szOption);
        return false;
    }
    if (pEquals != szArg + cchOption)
    {
        // Argument 'szArgs' starts with 'szOption' but it is longer
        return false;
    }

    if (FAILED(hr = anOutput.Configure(supportedTypes, pEquals + 1)))
    {
        spdlog::error(L"An error occurred when evaluating output for option /{}={}", szOption, pEquals + 1);
        return false;
    }
    if (hr == S_FALSE)
    {
        spdlog::error(L"None of the supported output for option /{} matched {}", szOption, szArg);
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
        spdlog::error(L"Option /{} should be like: /{}=c:\\temp\\OutputFile.csv", szOption, szOption);
        return false;
    }
    if (auto hr = GetOutputFile(pEquals + 1, strOutputFile, true); FAILED(hr))
    {
        spdlog::error(L"Invalid output dir specified: {}", pEquals + 1);
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
        spdlog::error(L"Option /{} should be like: /{}=c:\\temp", szOption, szOption);
        return false;
    }
    if (auto hr = GetOutputDir(pEquals + 1, strOutputDir, true); FAILED(hr))
    {
        spdlog::error(L"Invalid output dir specified: {}", pEquals + 1);
        return false;
    }
    return true;
}

bool UtilitiesMain::InputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        spdlog::error(L"Option /{} should be like: /{}=c:\\temp\\InputFile.csv", szOption, szOption);
        return false;
    }

    if (auto hr = ExpandFilePath(pEquals + 1, strOutputFile); FAILED(hr))
    {
        spdlog::error(L"Invalid input file specified: {}", pEquals + 1);
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
        spdlog::error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
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
        spdlog::error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
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
        spdlog::error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
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
        spdlog::error("Parameter is too big (>MAXDWORD)");
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
        spdlog::error(L"Option /{} should be like: /{}=<yes>|<no>", szOption, szOption);
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
            spdlog::error("Parameter is too big (>MAXDWORD)");
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
        spdlog::error(L"Option /{} should be like: /{}=32M", szArg, szOption);
        return false;
    }
    LARGE_INTEGER liSize;
    liSize.QuadPart = 0;
    if (FAILED(hr = GetFileSizeFromArg(pEquals + 1, liSize)))
    {
        spdlog::error(L"Option /{} Failed to convert into a size", szArg);
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
        spdlog::error(L"Option /{} should be like: /{}=highest|lowest|exact", szArg, szOption);
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

bool UtilitiesMain::ToggleBooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bPresent)
{
    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    bPresent = !bPresent;
    return true;
}

bool UtilitiesMain::CryptoHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, CryptoHashStream::Algorithm& algo)
{
    HRESULT hr = E_FAIL;

    if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
        return false;

    LPCWSTR pEquals = wcschr(szArg, L'=');
    if (!pEquals)
    {
        spdlog::error(L"Option /{} should be like: /{}=hash_functions", szArg, szOption);
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
            spdlog::warn(L"Hash algorithm '{}' is not supported", key);
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
        spdlog::error(L"Option /{} should be like: /{}=32M", szArg, szOption);
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
    spdlog::info("Waiting 30 seconds for a debugger to attach...");
    auto counter = 0LU;
    while (!IsDebuggerPresent() && counter < 60)
    {
        counter++;
        Sleep(500);
    }
    if (counter < 60)
        spdlog::info("Debugger connected!");
    else
        spdlog::info("No debugger connected... let's continue");
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
        || !_wcsnicmp(szArg, L"NoConsole", wcslen(L"NoConsole")))
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

bool UtilitiesMain::IgnoreCommonOptions(LPCWSTR szArg)
{
    if (IgnoreWaitForDebuggerOption(szArg))
        return true;
    if (IgnoreConfigOptions(szArg))
        return true;
    if (IgnoreLoggingOptions(szArg))
        return true;
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
            m_console.Write("({} {})", metaName, metaVersion);
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

void UtilitiesMain::PrintLoggingUsage()
{
    spdlog::info(
        L""
        L"\t/verbose                    : Turns on verbose logging"
        L"\t/debug                      : Adds debug information (Source File Name, Line number) to output, outputs to "
        L"debugger (OutputDebugString)"
        L"\t/noconsole                  : Turns off console logging"
        L"\t/logfile=<FileName>         : All output is duplicated to logfile <FileName>"
        L"");
}

void UtilitiesMain::PrintPriorityUsage()
{
    spdlog::info(
        L""
        L"\t/low                        : Runs with lowered priority\n"
        L"");
}

void UtilitiesMain::PrintStartTime()
{
    m_console.Print("Start time: {}", theStartTime);
}

void UtilitiesMain::PrintCommonParameters()
{
    using namespace Orc::Text;

    auto root = m_console.OutputTree();

    auto node = root.AddNode("Parameters");
    PrintValue(node, "Start time", theStartTime);

    std::wstring computerName;
    SystemDetails::GetComputerName_(computerName);
    PrintValue(node, "Computer name", computerName);

    std::wstring fullComputerName;
    SystemDetails::GetFullComputerName(fullComputerName);
    if (fullComputerName != computerName)
    {
        PrintValue(node, "Full computer name", fullComputerName);
    }

    std::wstring orcComputerName;
    SystemDetails::GetOrcComputerName(orcComputerName);
    if (computerName != orcComputerName)
    {
        PrintValue(node, "DFIR-Orc computer name", orcComputerName);
    }

    std::wstring orcFullComputerName;
    SystemDetails::GetOrcFullComputerName(orcFullComputerName);
    if (orcFullComputerName != fullComputerName && orcFullComputerName != orcComputerName)
    {
        PrintValue(node, "DFIR-Orc computer", orcFullComputerName);
    }

    std::wstring description;
    SystemDetails::GetDescriptionString(description);
    PrintValue(node, "Operating system", description);

    PrintValue(node, "Process ID", GetCurrentProcessId());
}
