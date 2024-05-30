//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"

#include "WolfExecution.h"

#include "ParameterCheck.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"

#include "CaseInsensitive.h"

#include "Configuration/ConfigFile.h"
#include "ConfigFile_WOLFLauncher.h"

#include <safeint.h>

#include <algorithm>
#include <string>

#include <boost/tokenizer.hpp>
#include "Utils/WinApi.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::Wolf;

std::wregex WolfExecution::g_WinVerRegEx(L"(\\d+)\\.(\\d+)((\\+)|(\\-))?");

constexpr auto WINVER_MAJOR = 1;
constexpr auto WINVER_MINOR = 2;
constexpr auto WINVER_PLUS = 4;
constexpr auto WINVER_MINUS = 5;

HRESULT
WolfExecution::GetExecutableToRun(const ConfigItem& item, wstring& strExeToRun, wstring& strArgToAdd, bool& isSelf)
{
    HRESULT hr = E_FAIL;
    WORD wArch = 0;

    if (FAILED(hr = SystemDetails::GetArchitecture(wArch)))
        return hr;

    wstring strExeRef;

    switch (wArch)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            if (item[WOLFLAUNCHER_EXERUN64])
                strExeRef = item[WOLFLAUNCHER_EXERUN64];
            else if (item[WOLFLAUNCHER_EXERUN])
                strExeRef = item[WOLFLAUNCHER_EXERUN];
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            if (item[WOLFLAUNCHER_EXERUN32])
                strExeRef = item[WOLFLAUNCHER_EXERUN32];
            else if (item[WOLFLAUNCHER_EXERUN])
                strExeRef = item[WOLFLAUNCHER_EXERUN];
            break;
        default:
            break;
    }
    if (strExeRef.empty())
    {
        Log::Debug(
            L"No valid binary specified for item name '{}' (run, run32 or run64 attribute)",
            item[WOLFLAUNCHER_EXENAME].c_str());
        return E_INVALIDARG;
    }

    if (EmbeddedResource::IsResourceBased(strExeRef))
    {
        wstring strModule, strResource, strCabbedName, strFormat;

        if (EmbeddedResource::IsSelf(strExeRef))
        {
            if (FAILED(EmbeddedResource::GetSelf(strExeToRun)))
                return hr;

            if (FAILED(EmbeddedResource::GetSelfArgument(strExeRef, strArgToAdd)))
                return hr;

            isSelf = true;
        }
        else
        {
            if (FAILED(
                    hr = EmbeddedResource::SplitResourceReference(
                        strExeRef, strModule, strResource, strCabbedName, strFormat)))
            {
                Log::Error(L"The resource specified '{}' could not be split [{}]", strExeRef, SystemError(hr));
                return E_FAIL;
            }

            HMODULE hModule;
            HRSRC hRes;
            std::wstring strBinaryPath;
            if (FAILED(
                    hr = EmbeddedResource::LocateResource(
                        strModule, strResource, EmbeddedResource::BINARY(), hModule, hRes, strBinaryPath)))
            {
                Log::Error(L"The resource specified '{}' could not be located [{}]", strExeRef, SystemError(hr));
                return E_FAIL;
            }
            else
            {
                FreeLibrary(hModule);
                hModule = NULL;
                strExeToRun = strExeRef;
            }
        }
    }
    else
    {
        std::error_code ec;
        std::wstring strExeFile = ExpandEnvironmentStringsApi(strExeRef.c_str(), ec);
        if (ec)
        {
            Log::Error("Failed to expand environment variables in uri [{}]", ec);
            return ToHRESULT(ec);
        }

        if (std::filesystem::exists(strExeFile, ec))
        {
            hr = VerifyFileIsBinary(strExeFile.c_str());
            if (FAILED(hr))
            {

                Log::Error(L"Executable file '{}' is not a compatible binary [{}]", strExeFile, SystemError(hr));
                return E_FAIL;
            }
        }

        strExeToRun = strExeFile;
    }

    return S_OK;
}

CommandMessage::Message WolfExecution::SetCommandFromConfigItem(const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    if (_wcsicmp(item.strName.c_str(), L"command"))
    {
        Log::Debug("item passed is not a command");
        return nullptr;
    }

    if (!item[WOLFLAUNCHER_COMMAND_CMDKEYWORD])
    {
        Log::Debug("keyword attribute is missing in command");
        return nullptr;
    }

    const wstring& keyword = item[WOLFLAUNCHER_COMMAND_CMDKEYWORD];

    if (item.SubItems[WOLFLAUNCHER_COMMAND_WINVER])
    {
        std::wsmatch s;
        std::wstring winver(item.SubItems[WOLFLAUNCHER_COMMAND_WINVER]);

        if (std::regex_match(winver, s, g_WinVerRegEx))
        {
            DWORD dwReqMajor = std::stoul(s[WINVER_MAJOR]);
            DWORD dwReqMinor = std::stoul(s[WINVER_MINOR]);
            bool bPlus = false, bMinus = false;

            if (s[WINVER_PLUS].matched)
                bPlus = true;
            if (s[WINVER_MINUS].matched)
                bMinus = true;

            DWORD dwWinMajor = 0, dwWinMinor = 0;
            if (FAILED(hr = SystemDetails::GetOSVersion(dwWinMajor, dwWinMinor)))
            {
                Log::Error("Could not get OS version, statement ignored [{}]", SystemError(hr));
                return nullptr;
            }
            else
            {
                if (bPlus)
                {
                    if (dwWinMajor < dwReqMajor)
                    {
                        Log::Debug(
                            L"Skipping command {}: OS major version {} is incompatible with required {}",
                            keyword,
                            dwWinMajor,
                            dwReqMajor);
                        return nullptr;
                    }
                    if (dwWinMajor == dwReqMajor && dwWinMinor < dwReqMinor)
                    {
                        Log::Debug(
                            L"Skipping command {}: OS minor version {} is incompatible with required {}",
                            keyword,
                            dwWinMinor,
                            dwReqMinor);
                        return nullptr;
                    }
                }
                else if (bMinus)
                {
                    if (dwWinMajor > dwReqMajor)
                    {
                        Log::Debug(
                            L"Skipping command {}: OS major version {} is incompatible with required {}",
                            keyword,
                            dwWinMajor,
                            dwReqMajor);
                        return nullptr;
                    }
                    if (dwWinMajor == dwReqMajor && dwWinMinor > dwReqMinor)
                    {
                        Log::Debug(
                            L"Skipping command {}: OS minor version {} is incompatible with required {}",
                            keyword,
                            dwWinMinor,
                            dwReqMinor);
                        return nullptr;
                    }
                }
                else
                {
                    if (dwWinMajor != dwReqMajor || dwWinMinor != dwReqMinor)
                    {
                        Log::Debug(
                            L"Skipping command '{}': OS version mismatch ({}.{} != required {}.{})",
                            keyword,
                            dwWinMajor,
                            dwWinMinor,
                            dwReqMajor,
                            dwReqMinor);
                        return nullptr;
                    }
                }
                Log::Debug(
                    L"Command '{}': OS version {}.{} compatible with requirement winver={}.{}{}",
                    keyword,
                    dwWinMajor,
                    dwWinMinor,
                    dwReqMajor,
                    dwReqMinor,
                    bPlus ? L"+" : L"");
            }
        }
        else
        {
            Log::Error(L"Specified winver attribute '{}' is invalid, ignored", winver);
        }
        // No incompatibilities found, proceed with command
    }

    if (item.SubItems[WOLFLAUNCHER_COMMAND_SYSTEMTYPE])
    {
        const wstring& requiredSystemTypes = item[WOLFLAUNCHER_COMMAND_SYSTEMTYPE];
        wstring strProductType;

        if (FAILED(hr = SystemDetails::GetOrcSystemType(strProductType)))
        {
            Log::Error("Failed to retrieve system product type [{}]", SystemError(hr));
            return nullptr;
        }

        boost::char_separator<wchar_t> sep(L"|", nullptr, boost::keep_empty_tokens);
        typedef boost::tokenizer<boost::char_separator<wchar_t>, std::wstring::const_iterator, std::wstring>
            mytokenizer;

        mytokenizer tokens(requiredSystemTypes, sep);
        bool bFound = false;
        for (auto tok_iter = begin(tokens); tok_iter != end(tokens); ++tok_iter)
        {
            if (equalCaseInsensitive(*tok_iter, strProductType))
            {
                Log::Debug(L"Found product type '{}' matching system's '{}'", tok_iter->c_str(), strProductType);
                bFound = true;
            }
        }
        if (!bFound)
        {
            Log::Debug(
                L"Command '{}' will not proceed ('{}' does not match any of '{}')",
                keyword,
                strProductType,
                requiredSystemTypes);
            return nullptr;
        }
    }

    auto command = CommandMessage::MakeExecuteMessage(keyword);

    if (item[WOLFLAUNCHER_COMMAND_EXECUTE])
    {
        const wstring& ExeName = item[WOLFLAUNCHER_COMMAND_EXECUTE][WOLFLAUNCHER_EXENAME];

        bool isSelf = false;
        std::wstring strExeToRun, strArgToAdd;
        if (FAILED(GetExecutableToRun(item[WOLFLAUNCHER_COMMAND_EXECUTE], strExeToRun, strArgToAdd, isSelf)))
            return nullptr;

        if (isSelf)
        {
            command->SetIsSelfOrcExecutable(isSelf);
            command->SetOrcTool(strArgToAdd);
        }

        command->PushExecutable(item[WOLFLAUNCHER_COMMAND_EXECUTE].dwOrderIndex, strExeToRun, ExeName);

        if (!strArgToAdd.empty())
            command->PushArgument(-1, strArgToAdd);
    }
    else
    {
        Log::Error("Execute element is missing");
        return nullptr;
    }

    if (item[WOLFLAUNCHER_COMMAND_ARGUMENT])
    {
        std::for_each(
            begin(item[WOLFLAUNCHER_COMMAND_ARGUMENT].NodeList),
            end(item[WOLFLAUNCHER_COMMAND_ARGUMENT].NodeList),
            [command](const ConfigItem& item) { command->PushArgument(item.dwOrderIndex, item); });
    }

    if (item[WOLFLAUNCHER_COMMAND_INPUT])
    {
        hr = S_OK;

        std::for_each(
            begin(item[WOLFLAUNCHER_COMMAND_INPUT].NodeList),
            end(item[WOLFLAUNCHER_COMMAND_INPUT].NodeList),
            [this, command, &hr](const ConfigItem& input) {
                HRESULT hr1 = E_FAIL;
                if (!input[WOLFLAUNCHER_INNAME])
                {
                    Log::Error("The input is missing a name");
                    hr = E_INVALIDARG;
                    return;
                }
                const wstring& strName = input[WOLFLAUNCHER_INNAME];

                const WCHAR* szPattern = NULL;
                if (input[WOLFLAUNCHER_INARGUMENT])
                {
                    szPattern = input[WOLFLAUNCHER_INARGUMENT].c_str();
                }

                if (!input[WOLFLAUNCHER_INSOURCE])
                {
                    Log::Error(L"The input '{}' is missing a source", input[WOLFLAUNCHER_INNAME].c_str());
                    hr = E_INVALIDARG;
                    return;
                }
                if (EmbeddedResource::IsResourceBased(input.SubItems[WOLFLAUNCHER_INSOURCE]))
                {
                    const wstring& szResName = input.SubItems[WOLFLAUNCHER_INSOURCE];

                    wstring strModule, strResource, strCabbedName, strFormat;
                    hr1 = EmbeddedResource::SplitResourceReference(
                        szResName, strModule, strResource, strCabbedName, strFormat);
                    if (FAILED(hr1))
                    {
                        Log::Error(L"The resource specified '{}' could not be split [{}]", szResName, SystemError(hr1));
                        hr = hr1;
                        return;
                    }

                    HMODULE hModule;
                    HRSRC hRes;
                    std::wstring strBinaryPath;
                    hr1 = EmbeddedResource::LocateResource(
                        strModule, strResource, EmbeddedResource::BINARY(), hModule, hRes, strBinaryPath);
                    if (FAILED(hr1))
                    {
                        Log::Error(
                            L"The resource specified '{}' could not be located [{}]", szResName, SystemError(hr1));
                        hr = hr1;
                        return;
                    }
                    else
                    {
                        FreeLibrary(hModule);
                        hModule = NULL;
                        if (szPattern != NULL)
                            command->PushInputFile(input.dwOrderIndex, szResName, strName, szPattern);
                        else
                            command->PushInputFile(input.dwOrderIndex, szResName, strName);
                    }
                }
                else
                {
                    wstring strInputFile;
                    if (SUCCEEDED(hr1 = ExpandFilePath(input[WOLFLAUNCHER_INSOURCE].c_str(), strInputFile)))
                    {
                        if (szPattern != NULL)
                            command->PushInputFile(input.dwOrderIndex, strInputFile, strName, szPattern);
                        else
                            command->PushInputFile(input.dwOrderIndex, strInputFile, strName);
                    }
                    else
                    {
                        Log::Error(
                            L"Input file '{}' does not exist or is not a file [{}]", strInputFile, SystemError(hr1));
                        hr = hr1;
                        return;
                    }
                }
            });
        if (FAILED(hr))
            return nullptr;
    }

    if (item[WOLFLAUNCHER_COMMAND_OUTPUT])
    {
        hr = S_OK;
        std::for_each(
            begin(item[WOLFLAUNCHER_COMMAND_OUTPUT].NodeList),
            end(item[WOLFLAUNCHER_COMMAND_OUTPUT].NodeList),
            [this, &hr, command](const ConfigItem& output) {
                if (!output.SubItems[WOLFLAUNCHER_OUTNAME])
                {
                    Log::Info("The output is missing a name");
                    hr = E_FAIL;
                    return;
                }
                const wstring& strName = output[WOLFLAUNCHER_OUTNAME];

                const WCHAR* szPattern = NULL;
                if (output[WOLFLAUNCHER_OUTARGUMENT])
                {
                    szPattern = output[WOLFLAUNCHER_OUTARGUMENT].c_str();
                }

                if (!output[WOLFLAUNCHER_OUTSOURCE])
                {
                    Log::Info(L"The output '{}' is missing a source", output[WOLFLAUNCHER_OUTNAME].c_str());
                    hr = E_FAIL;
                    return;
                }
                if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"StdOut"))
                {
                    command->PushStdOut(output.dwOrderIndex, strName, true, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"StdErr"))
                {
                    command->PushStdErr(output.dwOrderIndex, strName, true, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"StdOutErr"))
                {
                    command->PushStdOutErr(output.dwOrderIndex, strName, true, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"File"))
                {
                    if (szPattern == NULL)
                        command->PushOutputFile(output.dwOrderIndex, strName, strName, true);
                    else
                        command->PushOutputFile(output.dwOrderIndex, strName, strName, szPattern, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"Directory"))
                {
                    if (output[WOLFLAUNCHER_OUTFILEMATCH])
                    {
                        if (szPattern == NULL)
                            command->PushOutputDirectory(
                                output.dwOrderIndex, strName, strName, output[WOLFLAUNCHER_OUTFILEMATCH], true);
                        else
                            command->PushOutputDirectory(
                                output.dwOrderIndex,
                                strName,
                                strName,
                                output[WOLFLAUNCHER_OUTFILEMATCH],
                                szPattern,
                                true);
                    }
                    else
                    {
                        if (szPattern == NULL)
                            command->PushOutputDirectory(output.dwOrderIndex, strName, strName, L"*", true);
                        else
                            command->PushOutputDirectory(output.dwOrderIndex, strName, strName, L"*", szPattern, true);
                    }
                }
            });
        if (FAILED(hr))
            return nullptr;
    }

    if (item[WOLFLAUNCHER_COMMAND_QUEUE])
    {
        if (!_wcsicmp(item[WOLFLAUNCHER_COMMAND_QUEUE].c_str(), L"flush"))
        {
            command->SetQueueBehavior(CommandMessage::QueueBehavior::FlushQueue);
        }
        else
        {
            command->SetQueueBehavior(CommandMessage::QueueBehavior::Enqueue);
        }
    }

    if (item[WOLFLAUNCHER_COMMAND_OPTIONAL])
    {
        if (!_wcsicmp(item[WOLFLAUNCHER_COMMAND_OPTIONAL].c_str(), L"no"))
        {
            command->SetMandatory();
        }
        else
        {
            command->SetOptional();
        }
    }

    if (item[WOLFLAUNCHER_COMMAND_TIMEOUT])
    {
        LARGE_INTEGER li;
        hr = GetIntegerFromArg(item[WOLFLAUNCHER_COMMAND_TIMEOUT].c_str(), li);
        if (FAILED(hr))
        {
            Log::Debug(L"Failed to initialize command timeout [{}]", SystemError(hr));
            return nullptr;
        }

        std::chrono::minutes timeout(li.QuadPart);
        command->SetTimeout(timeout);
    }

    return command;
}

HRESULT WolfExecution::SetRestrictionsFromConfig(const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    if (_wcsicmp(item.strName.c_str(), L"restrictions"))
    {
        Log::Debug("item passed is not restrictions");
        return E_INVALIDARG;
    }

    WORD arch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(arch)))
    {
        Log::Warn("Failed to retrieve architecture [{}]", SystemError(hr));
        return hr;
    }

    if (item[WOLFLAUNCHER_JOBMEMORY])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetFileSizeFromArg(item[WOLFLAUNCHER_JOBMEMORY].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            Log::Warn(
                L"Specified size is too big for JOB MEMORY restriction '{}'", item[WOLFLAUNCHER_JOBMEMORY].c_str());
        }
        else
        {
            if (!m_Restrictions.ExtendedLimits)
            {
                m_Restrictions.ExtendedLimits.emplace();
                ZeroMemory(&m_Restrictions.ExtendedLimits.value(), sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
            }
            m_Restrictions.ExtendedLimits->JobMemoryLimit = msl::utilities::SafeInt<SIZE_T>(li.QuadPart);
            m_Restrictions.ExtendedLimits->BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
        }
    }

    if (item[WOLFLAUNCHER_PROCESSMEMORY])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetFileSizeFromArg(item[WOLFLAUNCHER_PROCESSMEMORY].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            Log::Warn(
                L"Specified size is too big for PROCESS memory restriction '{}'",
                item[WOLFLAUNCHER_PROCESSMEMORY].c_str());
        }
        else
        {
            if (!m_Restrictions.ExtendedLimits)
            {
                m_Restrictions.ExtendedLimits.emplace();
                ZeroMemory(&m_Restrictions.ExtendedLimits.value(), sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
            }
            m_Restrictions.ExtendedLimits->ProcessMemoryLimit = msl::utilities::SafeInt<SIZE_T>(li.QuadPart);
            m_Restrictions.ExtendedLimits->BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        }
    }

    if (item[WOLFLAUNCHER_ELAPSEDTIME])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(item[WOLFLAUNCHER_ELAPSEDTIME].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            Log::Critical(
                L"Specified size is too big for elapsed time restriction '{}'", item[WOLFLAUNCHER_ELAPSEDTIME].c_str());
        }
        // Elapsed time is expressed in minutes
        m_ElapsedTime = std::chrono::minutes(li.LowPart);
    }

    if (item[WOLFLAUNCHER_JOBUSERTIME])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(item[WOLFLAUNCHER_JOBUSERTIME].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            Log::Warn(
                L"Specified size is too big for JOB user time restriction '{}'",
                item[WOLFLAUNCHER_JOBUSERTIME].c_str());
        }
        // CPU time is expressed in minutes
        if (!m_Restrictions.ExtendedLimits)
        {
            m_Restrictions.ExtendedLimits.emplace();
            ZeroMemory(&m_Restrictions.ExtendedLimits.value(), sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        }
        m_Restrictions.ExtendedLimits.value().BasicLimitInformation.PerJobUserTimeLimit.QuadPart =
            li.QuadPart * 60 * 1000 * 1000 * 10;
        m_Restrictions.ExtendedLimits.value().BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_TIME;
    }

    if (item[WOLFLAUNCHER_PERPROCESSUSERTIME])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(item[WOLFLAUNCHER_PERPROCESSUSERTIME].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            Log::Warn(
                L"Specified size is too big for PROCESS user time restriction '{}'",
                item[WOLFLAUNCHER_PERPROCESSUSERTIME].c_str());
        }

        if (!m_Restrictions.ExtendedLimits)
        {
            m_Restrictions.ExtendedLimits.emplace();
            ZeroMemory(&m_Restrictions.ExtendedLimits.value(), sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        }
        // CPU time is expressed in minutes
        m_Restrictions.ExtendedLimits->BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
            li.QuadPart * 60 * 1000 * 1000 * 10;
        m_Restrictions.ExtendedLimits->BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
    }

    if (item[WOLFLAUNCHER_CPU_RATE] && item[WOLFLAUNCHER_CPU_WEIGHT])
    {
        Log::Warn("CpuRate and CpuWeight are mutually exclusive: values ignored");
    }
    else if (item[WOLFLAUNCHER_CPU_RATE])
    {
        DWORD percentage = 0;
        if (FAILED(hr = GetPercentageFromArg(item[WOLFLAUNCHER_CPU_RATE].c_str(), percentage)))
            return hr;

        if (!m_Restrictions.CpuRateControl)
        {
            m_Restrictions.CpuRateControl.emplace();
            ZeroMemory(&m_Restrictions.CpuRateControl.value(), sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION));
        }

        m_Restrictions.CpuRateControl->ControlFlags =
            JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        m_Restrictions.CpuRateControl->CpuRate = percentage * 100;
    }
    else if (item[WOLFLAUNCHER_CPU_WEIGHT])
    {
        DWORD weight = 0;
        if (FAILED(hr = GetIntegerFromArg(item[WOLFLAUNCHER_CPU_WEIGHT].c_str(), weight)))
            return hr;

        if (!m_Restrictions.CpuRateControl)
        {
            m_Restrictions.CpuRateControl.emplace();
            ZeroMemory(&m_Restrictions.CpuRateControl.value(), sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION));
        }

        if (weight > 9 || weight < 1)
        {
            Log::Warn("CpuWeight valid values are (1-9), current invalid value {} is ignored", weight);
        }
        else
        {
            m_Restrictions.CpuRateControl->ControlFlags =
                JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED;
            m_Restrictions.CpuRateControl->Weight = weight;
        }
    }

    return S_OK;
}

HRESULT WolfExecution::SetCommandsFromConfig(const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    if (_wcsicmp(item.strName.c_str(), L"command"))
    {
        Log::Debug("item passed is not a \"command\"");
        return E_INVALIDARG;
    }
    m_dwLongerTaskKeyword = 0L;

    for (const auto& an_item : item.NodeList)
    {

        auto command = SetCommandFromConfigItem(an_item);

        if (command != nullptr)
        {
            m_dwLongerTaskKeyword =
                std::max<DWORD>(m_dwLongerTaskKeyword, static_cast<DWORD>(command->Keyword().size() / sizeof(WCHAR)));
            m_Commands.push_back(command);
        }
    }
    return S_OK;
}

HRESULT WolfExecution::SetJobTimeOutFromConfig(
    const ConfigItem& item,
    std::chrono::milliseconds msCmdTimeOut,
    std::chrono::milliseconds msArchiveTimeOut)
{
    HRESULT hr = S_OK;

    if (!item[WOLFLAUNCHER_ARCHIVE_CMD_TIMEOUT])
    {
        m_CmdTimeOut = msCmdTimeOut;
    }
    else
    {
        m_CmdTimeOut = std::chrono::minutes((DWORD32)item[WOLFLAUNCHER_ARCHIVE_CMD_TIMEOUT]);
    }

    if (!item[WOLFLAUNCHER_ARCHIVE_TIMEOUT])
    {
        m_ArchiveTimeOut = msArchiveTimeOut;
    }
    else
    {
        m_ArchiveTimeOut = std::chrono::minutes((DWORD32)item[WOLFLAUNCHER_ARCHIVE_TIMEOUT]);
    }

    return S_OK;
}

HRESULT WolfExecution::SetJobConfigFromConfig(const ConfigItem& item)
{
    // <Archive name="Robustness.cab" keyword="Robustness" compression="fast" >

    HRESULT hr = E_FAIL;

    if (_wcsicmp(item.strName.c_str(), L"archive"))
    {
        Log::Debug("item is not an archive item");
        return E_INVALIDARG;
    }

    if (!item[WOLFLAUNCHER_ARCHIVE_KEYWORD])
    {
        Log::Debug("keyword attribute is missing in archive");
        return E_INVALIDARG;
    }
    else
        m_commandSet = item[WOLFLAUNCHER_ARCHIVE_KEYWORD];

    if (item[WOLFLAUNCHER_ARCHIVE_COMPRESSION])
        m_strCompressionLevel = (const std::wstring&)item[WOLFLAUNCHER_ARCHIVE_COMPRESSION];

    if (!item[WOLFLAUNCHER_ARCHIVE_CONCURRENCY])
    {
        m_dwConcurrency = 5;
    }
    else
    {
        m_dwConcurrency = (DWORD32)item[WOLFLAUNCHER_ARCHIVE_CONCURRENCY];
    }

    return S_OK;
}

HRESULT WolfExecution::SetRepeatBehaviourFromConfig(const ConfigItem& item)
{
    HRESULT E_FAIL;

    if (item)
    {
        if (!_wcsicmp(item.c_str(), L"CreateNew"))
            m_RepeatBehavior = Repeat::CreateNew;
        else if (!_wcsicmp(item.c_str(), L"Overwrite"))
            m_RepeatBehavior = Repeat::Overwrite;
        else if (!_wcsicmp(item.c_str(), L"Once"))
            m_RepeatBehavior = Repeat::Once;
        else
        {
            Log::Error(
                L"Invalid repeat behaviour in config: '{}' (allowed values are CreateNew, Overwrite, Once)", item);
            return E_INVALIDARG;
        }
    }
    return S_OK;
}

HRESULT WolfExecution::SetRepeatBehaviour(const Repeat behavior)
{
    if (behavior != Repeat::NotSet)
        m_RepeatBehavior = behavior;

    if (m_RepeatBehavior == Repeat::NotSet)
    {
        // Default behaviour is, by default, overwrite
        m_RepeatBehavior = Repeat::Overwrite;
    }

    return S_OK;
}

HRESULT WolfExecution::SetCompressionLevel(const std::wstring& level)
{
    if (level.empty())
    {
        Log::Debug("Specified compression level is empty");
        return S_OK;
    }

    Log::Debug(L"Set compression level to {}", level);
    m_strCompressionLevel = level;
    return S_OK;
}
