//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "UtilitiesMain.h"

#include "LogFileWriter.h"
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

#include <Psapi.h>

#include <filesystem>
#include <set>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost\scope_exit.hpp>

using namespace std;

using namespace Orc;
using namespace Orc::Command;

bool UtilitiesMain::IsProcessParent(LPCWSTR szImageName)
{
    HRESULT hr = E_FAIL;
    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>(_L_);

    if (pNtDll == nullptr)
        return false;

    PROCESS_BASIC_INFORMATION pbi;
    ZeroMemory(&pbi, sizeof(PROCESS_BASIC_INFORMATION));
    if (FAILED(pNtDll->NtQueryInformationProcess(
            GetCurrentProcess(), ProcessBasicInformation, &pbi, (ULONG)sizeof(PROCESS_BASIC_INFORMATION), nullptr)))
    {
        log::Error(_L_, E_FAIL, L"NtQueryInformationProcess failed\r\n");
        return false;
    }

    DWORD dwParentId = static_cast<DWORD>(reinterpret_cast<DWORD64>(pbi.Reserved3));

    HANDLE hParent = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        dwParentId);  // Reserved3 member contains the parent process ID
    if (hParent == NULL)
    {
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"OpenProcess failed for parent process pid: %d\r\n", dwParentId);
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
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"GetProcessImageFileName failed\r\n");
        return false;
    }

    log::Verbose(_L_, L"Parent process file name is %s\r\n", szImageFileName);

    WCHAR szImageBaseName[MAX_PATH];
    if (FAILED(hr = GetFileNameForFile(szImageFileName, szImageBaseName, MAX_PATH)))
    {
        log::Error(_L_, hr, L"Failed to build filename for %s\r\n", szImageFileName);
        return false;
    }

    if (_wcsicmp(szImageBaseName, szImageName))
        return false;

    return true;
}

HRESULT Orc::Command::UtilitiesMain::LoadCommonExtensions()
{
    auto ntdll = ExtensionLibrary::GetLibrary<NtDllExtension>(_L_);
    if (!ntdll)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize ntdll extension\r\n");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(ntdll));

    auto kernel32 = ExtensionLibrary::GetLibrary<Kernel32Extension>(_L_);
    if (!kernel32)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize kernel extension\r\n");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(kernel32));

    auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);
    if (!xmllite)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize xmllite extension\r\n");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(xmllite));

    return S_OK;
}

HRESULT Orc::Command::UtilitiesMain::LoadWinTrust()
{
    auto wintrust = ExtensionLibrary::GetLibrary<WinTrustExtension>(_L_);
    if (!wintrust)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize wintrust extension\r\n");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(wintrust));
    return S_OK;
}

HRESULT Orc::Command::UtilitiesMain::LoadEvtLibrary()
{
    auto wevtapi = ExtensionLibrary::GetLibrary<EvtLibrary>(_L_);
    if (!wevtapi)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize wevtapi extension\r\n");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(wevtapi));
    return S_OK;
}

HRESULT Orc::Command::UtilitiesMain::LoadPSAPI()
{
    auto psapi = ExtensionLibrary::GetLibrary<PSAPIExtension>(_L_);
    if (!psapi)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize psapi extension\r\n");
        return E_FAIL;
    }
    m_extensions.push_back(std::move(psapi));
    return S_OK;
}

HRESULT UtilitiesMain::SaveAndPrintStartTime()
{
    GetSystemTime(&theStartTime);
    theStartTickCount = GetTickCount();

    log::Info(
        _L_,
        L"\r\nStart time            : %02d/%02d/%04d %02d:%02d:%02d.%03d (UTC)\r\n",
        theStartTime.wMonth,
        theStartTime.wDay,
        theStartTime.wYear,
        theStartTime.wHour,
        theStartTime.wMinute,
        theStartTime.wSecond,
        theStartTime.wMilliseconds);
    return S_OK;
}

HRESULT UtilitiesMain::PrintSystemType()
{
    wstring strSystemType;
    SystemDetails::GetSystemType(strSystemType);

    log::Info(_L_, L"\r\nSystem type           : %s\r\n", strSystemType.c_str());
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

    log::Info(_L_, L"\r\nSystem tags           : %s\r\n", strTags.c_str());
    return S_OK;
}

HRESULT UtilitiesMain::PrintComputerName()
{
    wstring strComputerName;
    SystemDetails::GetComputerName_(strComputerName);
    log::Info(_L_, L"\r\nComputer              : %s\r\n", strComputerName.c_str());

    wstring strFullComputerName;
    SystemDetails::GetFullComputerName(strFullComputerName);

    if (strFullComputerName != strComputerName)
        log::Info(_L_, L"Full Computer         : %s\r\n", strFullComputerName.c_str());

    wstring strOrcComputerName;
    SystemDetails::GetOrcComputerName(strOrcComputerName);
    if (strComputerName != strOrcComputerName)
        log::Info(_L_, L"DFIR-Orc Computer     : %s\r\n", strOrcComputerName.c_str());

    wstring strOrcFullComputerName;
    SystemDetails::GetOrcFullComputerName(strOrcFullComputerName);

    if (strOrcFullComputerName != strFullComputerName && strOrcFullComputerName != strOrcComputerName)
        log::Info(_L_, L"DFIR-Orc Full Computer: %s\r\n", strOrcFullComputerName.c_str());
    return S_OK;
}

HRESULT UtilitiesMain::PrintWhoAmI()
{
    wstring strUserName;
    SystemDetails::WhoAmI(strUserName);
    bool bIsElevated = false;
    SystemDetails::AmIElevated(bIsElevated);
    log::Info(_L_, L"\r\nUser                  : %s%s\r\n", strUserName.c_str(), bIsElevated ? L" (elevated)" : L"");
    return S_OK;
}

HRESULT UtilitiesMain::PrintOperatingSystem()
{
    wstring strDesc;
    SystemDetails::GetDescriptionString(strDesc);
    log::Info(_L_, L"Operating System      : %s\r\n", strDesc.c_str());
    return S_OK;
}

HRESULT UtilitiesMain::PrintExecutionTime()
{
    GetSystemTime(&theFinishTime);
    theFinishTickCount = GetTickCount();
    DWORD dwElapsed;

    log::Info(
        _L_,
        L"Finish time           : %02u/%02u/%04u %02u:%02u:%02u.%03u (UTC)\r\n",
        theFinishTime.wMonth,
        theFinishTime.wDay,
        theFinishTime.wYear,
        theFinishTime.wHour,
        theFinishTime.wMinute,
        theFinishTime.wSecond,
        theFinishTime.wMilliseconds);
    if (theFinishTickCount < theStartTickCount)
        dwElapsed = (MAXDWORD - theStartTickCount) + theFinishTickCount;
    else
        dwElapsed = theFinishTickCount - theStartTickCount;

    DWORD dwMillisec = dwElapsed % 1000;
    DWORD dwSec = (dwElapsed / 1000) % 60;
    DWORD dwMin = (dwElapsed / (1000 * 60)) % 60;
    DWORD dwHour = (dwElapsed / (1000 * 60 * 60)) % 24;
    DWORD dwDay = (dwElapsed / (1000 * 60 * 60 * 24));

    log::Info(_L_, (L"Elapsed time          : "));
    if (dwDay)
        log::Info(_L_, L"%u days, ", dwDay);
    if (dwHour)
        log::Info(_L_, L"%u hour(s), ", dwHour);
    if (dwMin)
        log::Info(_L_, L"%u min(s), ", dwMin);
    if (dwSec)
        log::Info(_L_, L"%u sec(s), ", dwSec);
    log::Info(_L_, L"%u msecs\r\n", dwMillisec);

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
            return L" (encoding=?????)";
    }
}

LPCWSTR UtilitiesMain::GetXOR(DWORD dwXOR)
{
    if (dwXOR)
    {
        static WCHAR szStatement[] = L" (XOR with 0xFFFFFFFF)";
        swprintf_s(szStatement, L"(XOR with 0x%8.8X)", dwXOR);
        return szStatement;
    }
    return L"";
}

LPCWSTR UtilitiesMain::GetCompression(const std::wstring& strCompression)
{
    if (strCompression.empty())
        return L"";
    static WCHAR szCompression[MAX_PATH];
    swprintf_s(szCompression, MAX_PATH, L" (compression %s)", strCompression.c_str());
    return szCompression;
}

HRESULT UtilitiesMain::PrintOutputOption(LPCWSTR szOutputName, const OutputSpec& anOutput)
{
    if (anOutput.Path.empty() && anOutput.Type != OutputSpec::Kind::SQL)
    {
        log::Info(_L_, L"%-8.8s              : Empty\r\n", szOutputName);
        return S_OK;
    }

    switch (anOutput.Type)
    {
        case OutputSpec::Kind::Directory:
            log::Info(
                _L_,
                L"%-8.8s directory    : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::Archive:
            log::Info(
                _L_,
                L"%-8.8s archive      : %s%s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding),
                GetCompression(anOutput.Compression));
            break;
        case OutputSpec::Kind::TableFile:
            log::Info(
                _L_,
                L"%-8.8s table        : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::CSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV:
            log::Info(
                _L_,
                L"%-8.8s CSV          : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::TSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV:
            log::Info(
                _L_,
                L"%-8.8s TSV          : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::Parquet:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet:
            log::Info(
                _L_,
                L"%-8.8s Parquet      : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::ORC:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC:
            log::Info(
                _L_,
                L"%-8.8s Parquet      : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::StructuredFile:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON:
            log::Info(
                _L_,
                L"%-8.8s structured   : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::File:
            log::Info(
                _L_,
                L"%-8.8s file         : %s%s%s\r\n",
                szOutputName,
                anOutput.Path.c_str(),
                GetXOR(anOutput.XOR),
                GetEncoding(anOutput.OutputEncoding));
            break;
        case OutputSpec::Kind::SQL:
            log::Info(
                _L_,
                L"%-8.8s SQLServer  : %s (Table=%s)\r\n",
                szOutputName,
                anOutput.ConnectionString.c_str(),
                anOutput.TableName.c_str());
            break;
        case OutputSpec::Kind::None:
            log::Info(_L_, L"%-8.8s file         : None\r\n", szOutputName);
            break;
        default:
            log::Info(_L_, L"%-8.8s              : Unsupported\r\n", szOutputName);
            break;
    }

    if (anOutput.UploadOutput != nullptr)
    {
        log::Info(_L_, L"   Upload configuration:\r\n", szOutputName);
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
        log::Info(_L_, L"   --> Method         : %s\r\n", szMethod);

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
        log::Info(_L_, L"   --> Operation      : %s\r\n", szOperation);

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
        log::Info(_L_, L"   --> Mode           : %s\r\n", szMode);

        if (!anOutput.UploadOutput->JobName.empty())
        {
            log::Info(_L_, L"   --> Job name       : %s\r\n", anOutput.UploadOutput->JobName.c_str());
        }
        log::Info(
            _L_,
            L"   --> Server name    : %s, path = %s\r\n",
            anOutput.UploadOutput->ServerName.c_str(),
            anOutput.UploadOutput->RootPath.c_str());
        if (!anOutput.UploadOutput->UserName.empty())
        {
            log::Info(_L_, L"   --> User name      : %s\r\n", anOutput.UploadOutput->UserName.c_str());
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
        log::Info(_L_, L"   --> Auth Scheme    : %s\r\n", szAuthScheme);
    }

    return S_OK;
}

HRESULT UtilitiesMain::PrintBooleanOption(LPCWSTR szOptionName, bool bValue)
{
    if (bValue)
    {
        log::Info(_L_, L"%-17.17s     : On\r\n", szOptionName);
    }
    else
    {
        log::Info(_L_, L"%-17.17s     : Off\r\n", szOptionName);
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintBooleanOption(LPCWSTR szOptionName, const boost::logic::tribool& bValue)
{
    if (bValue)
    {
        log::Info(_L_, L"%-17.17s    : On\r\n", szOptionName);
    }
    else if (!bValue)
    {
        log::Info(_L_, L"%-17.17s    : Off\r\n", szOptionName);
    }
    else
    {
        log::Info(_L_, L"%-17.17s    : Indeterminate\r\n", szOptionName);
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintIntegerOption(LPCWSTR szOptionName, DWORD dwOption)
{
    log::Info(_L_, L"%-17.17s     : %d\r\n", szOptionName, dwOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintIntegerOption(LPCWSTR szOptionName, LONGLONG llOption)
{
    log::Info(_L_, L"%-17.17s     : %I64d\r\n", szOptionName, llOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintIntegerOption(LPCWSTR szOptionName, ULONGLONG ullOption)
{
    log::Info(_L_, L"%-17.17s     : %I64d\r\n", szOptionName, ullOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintHashAlgorithmOption(LPCWSTR szOptionName, SupportedAlgorithm algs)
{
    if (algs == SupportedAlgorithm::Undefined)
    {
        log::Info(_L_, L"%-17.17s     : None\r\n", szOptionName);
    }
    else
    {
        log::Info(_L_, L"%-17.17s     : %s\r\n", szOptionName, CryptoHashStream::GetSupportedAlgorithm(algs).c_str());
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintHashAlgorithmOption(LPCWSTR szOptionName, FuzzyHashStream::SupportedAlgorithm algs)
{
    if (algs == FuzzyHashStream::SupportedAlgorithm::Undefined)
    {
        log::Info(_L_, L"%-17.17s     : None\r\n", szOptionName);
    }
    else
    {
        log::Info(_L_, L"%-17.17s     : %s\r\n", szOptionName, FuzzyHashStream::GetSupportedAlgorithm(algs).c_str());
    }
    return S_OK;
}

HRESULT UtilitiesMain::PrintStringOption(LPCWSTR szOptionName, LPCWSTR szOption)
{
    log::Info(_L_, L"%-17.17s     : %s\r\n", szOptionName, szOption);
    return S_OK;
}

HRESULT UtilitiesMain::PrintFormatedStringOption(LPCWSTR szOptionName, LPCWSTR szFormat, va_list argList)
{
    log::Info(_L_, L"%-17.17s     : ", szOptionName);
    _L_->WriteFormatedString(szFormat, argList);
    return S_OK;
}

HRESULT UtilitiesMain::PrintFormatedStringOption(LPCWSTR szOptionName, LPCWSTR szFormat, ...)
{
    log::Info(_L_, L"%-17.17s     : ", szOptionName);
    va_list argList;
    va_start(argList, szFormat);
    _L_->WriteFormatedString(szFormat, argList);
    va_end(argList);
    return S_OK;
}

bool UtilitiesMain::OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec::Kind supportedTypes, OutputSpec& anOutput)
{
    HRESULT hr = E_FAIL;

    const auto cchOption = wcslen(szOption);
    if (!_wcsnicmp(szArg, szOption, cchOption))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=c:\\temp\r\n", szOption, szOption);
            return false;
        }
        else
        {
            if (pEquals != szArg + cchOption)
            {
                // Argument 'szArgs' starts with 'szOption' but it is longer
                return false;
            }

            if (FAILED(hr = anOutput.Configure(_L_, supportedTypes, pEquals + 1)))
            {
                log::Error(
                    _L_,
                    E_INVALIDARG,
                    L"An error occured when evaluating output for option /%s=%s\r\n",
                    szOption,
                    pEquals + 1);
                return false;
            }
            else if (hr == S_FALSE)
            {
                log::Info(_L_, L"WARNING: None of the supported output for option /%s matched %s\r\n", szOption, szArg);
                return false;
            }
            return true;
        }
    }
    return false;
}

bool UtilitiesMain::OutputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(
                _L_, E_INVALIDARG, L"Option /%s should be like: /%s=c:\\temp\\OutputFile.csv\r\n", szOption, szOption);
            return false;
        }
        else
        {
            if (FAILED(GetOutputFile(pEquals + 1, strOutputFile)))
            {
                log::Error(_L_, E_INVALIDARG, L"Invalid output dir specified: %s\r\n", pEquals + 1);
                return false;
            }
        }
        return true;
    }
    return false;
}

bool UtilitiesMain::OutputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputDir)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=c:\\temp\r\n", szOption, szOption);
            return false;
        }
        else
        {
            if (FAILED(GetOutputDir(pEquals + 1, strOutputDir)))
            {
                log::Error(_L_, E_INVALIDARG, L"Invalid output dir specified: %s\r\n", pEquals + 1);
                return false;
            }
        }
        return true;
    }
    return false;
}

bool UtilitiesMain::InputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(
                _L_, E_INVALIDARG, L"Option /%s should be like: /%s=c:\\temp\\InputFile.csv\r\n", szOption, szOption);
            return false;
        }
        else
        {
            if (FAILED(GetInputFile(pEquals + 1, strOutputFile)))
            {
                log::Error(_L_, E_INVALIDARG, L"Invalid input file specified: %s\r\n", pEquals + 1);
                return false;
            }
        }
        return true;
    }
    return false;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=<Value>\r\n", szOption, szOption);
            return false;
            ;
        }
        else
        {
            strParameter = pEquals + 1;
        }
        return true;
    }
    return false;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=<Value>\r\n", szOption, szOption);
            return false;
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
    return false;
}

bool UtilitiesMain::ParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=<Value>\r\n", szOption, szOption);
            return false;
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
                log::Error(_L_, E_INVALIDARG, L"Parameter is too big (>MAXDWORD)\r\n");
            }
            dwParameter = li.LowPart;
        }
        return true;
    }
    return false;
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
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=<yes>|<no>\r\n", szOption, szOption);
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
    return false;
}

bool UtilitiesMain::OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
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
    return false;
}

bool UtilitiesMain::OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
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
    return false;
}

bool UtilitiesMain::OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
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
                log::Error(_L_, E_INVALIDARG, L"Parameter is too big (>MAXDWORD)\r\n");
            }
            dwParameter = li.LowPart;
        }
        return true;
    }
    return false;
}

bool UtilitiesMain::FileSizeOption(LPCWSTR szArg, LPCWSTR szOption, DWORDLONG& dwlFileSize)
{
    HRESULT hr = E_FAIL;

    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=32M\r\n", szArg, szOption);
            return false;
        }
        else
        {
            LARGE_INTEGER liSize;
            liSize.QuadPart = 0;
            if (FAILED(hr = GetFileSizeFromArg(pEquals + 1, liSize)))
            {
                log::Error(_L_, E_INVALIDARG, L"Option /%s Failed to convert into a size\r\n", szArg);
                return false;
            }
            dwlFileSize = liSize.QuadPart;
        }
        return true;
    }
    return false;
}

bool UtilitiesMain::AltitudeOption(LPCWSTR szArg, LPCWSTR szOption, LocationSet::Altitude& altitude)
{
    HRESULT hr = E_FAIL;

    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=highest|lowest|exact\r\n", szArg, szOption);
            return false;
        }
        else
        {
            altitude = LocationSet::GetAltitudeFromString(pEquals + 1);
            return true;
        }
    }
    return false;
}

bool UtilitiesMain::BooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bPresent)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        bPresent = true;
        return true;
    }
    return false;
}

bool UtilitiesMain::BooleanOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bPresent)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        bPresent = true;
        return true;
    }
    return false;
}

bool UtilitiesMain::ToggleBooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bPresent)
{
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        bPresent = !bPresent;
        return true;
    }
    return false;
}

bool UtilitiesMain::CryptoHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, SupportedAlgorithm& algo)
{
    HRESULT hr = E_FAIL;

    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=hash_functions\r\n", szArg, szOption);
            return false;
        }
        else
        {
            std::set<wstring> keys;
            std::wstring values = pEquals + 1;
            boost::split(keys, values, boost::is_any_of(L","));

            for (const auto& key : keys)
            {
                algo = static_cast<SupportedAlgorithm>(CryptoHashStream::GetSupportedAlgorithm(key.c_str()) | algo);
            }

            return true;
        }
    }
    return false;
}

bool UtilitiesMain::FuzzyHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, FuzzyHashStream::SupportedAlgorithm& algo)
{
    HRESULT hr = E_FAIL;

    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        LPCWSTR pEquals = wcschr(szArg, L'=');
        if (!pEquals)
        {
            log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=32M\r\n", szArg, szOption);
            return false;
        }
        else
        {
            std::set<wstring> keys;
            std::wstring values = pEquals + 1;
            boost::split(keys, values, boost::is_any_of(L","));

            for (const auto& key : keys)
            {
                algo = static_cast<FuzzyHashStream::SupportedAlgorithm>(
                    FuzzyHashStream::GetSupportedAlgorithm(key.c_str()) | algo);
            }
            return true;
        }
    }
    return false;
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
    if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
    {
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        return true;
    }
    return false;
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
    if (!_wcsnicmp(szArg, L"WaitForDebugger", wcslen(L"WaitForDebugger")))
    {
        log::Info(_L_, L"Waiting 30 seconds for a debugger to attach...\r\n");
        auto counter = 0LU;
        while (!IsDebuggerPresent() && counter < 60)
        {
            counter++;
            Sleep(500);
        }
        if (counter < 60)
            log::Info(_L_, L"Debugger connected!\r\n");
        else
            log::Info(_L_, L"No debugger connected... let's continue\r\n");
        return true;
    }
    return false;
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
    if (!_wcsnicmp(szArg, L"Verbose", wcslen(L"Verbose")) || !_wcsnicmp(szArg, L"Debug", wcslen(L"Debug"))
        || !_wcsnicmp(szArg, L"LogFile", wcslen(L"LogFile")) || !_wcsnicmp(szArg, L"NoConsole", wcslen(L"NoConsole")))
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

void UtilitiesMain::PrintHeader(LPCWSTR szToolName, LPCWSTR szVersion)
{
    if (szToolName)
        log::Info(_L_, L"\r\n%s Version %s\r\n", szToolName, szVersion);
}

UtilitiesMain::~UtilitiesMain(void) {}
