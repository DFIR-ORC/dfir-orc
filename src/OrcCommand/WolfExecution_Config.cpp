//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <string>

#include "WolfLauncher.h"

#include "WolfExecution.h"

#include "ParameterCheck.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"

#include "CaseInsensitive.h"

#include "ConfigFile.h"
#include "ConfigFile_WOLFLauncher.h"

#include <boost/tokenizer.hpp>
#include <algorithm>

using namespace std;

using namespace Orc;
using namespace Orc::Command::Wolf;

std::wregex WolfExecution::g_WinVerRegEx(L"(\\d+)\\.(\\d+)((\\+)|(\\-))?");

constexpr auto WINVER_MAJOR = 1;
constexpr auto WINVER_MINOR = 2;
constexpr auto WINVER_PLUS = 4;
constexpr auto WINVER_MINUS = 5;

HRESULT WolfExecution::GetExecutableToRun(const ConfigItem& item, wstring& strExeToRun, wstring& strArgToAdd)
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
        log::Verbose(
            _L_,
            L"No valid binary specified for item name %s (run, run32 or run64 attribute)\r\n",
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
        }
        else
        {
            if (FAILED(
                    hr = EmbeddedResource::SplitResourceReference(
                        _L_, strExeRef, strModule, strResource, strCabbedName, strFormat)))
            {
                log::Error(_L_, hr, L"The ressource specified (%s) could not be split\r\n", strExeRef.c_str());
                return E_FAIL;
            }

            HMODULE hModule;
            HRSRC hRes;
            std::wstring strBinaryPath;
            if (FAILED(
                    hr = EmbeddedResource::LocateResource(
                        _L_, strModule, strResource, EmbeddedResource::BINARY(), hModule, hRes, strBinaryPath)))
            {
                log::Error(_L_, hr, L"The ressource specified (%s) could not be located\r\n", strExeRef.c_str());
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
        wstring strExeFile;
        if (SUCCEEDED(hr = GetInputFile(strExeRef.c_str(), strExeFile)))
        {
            if (VerifyFileIsBinary(strExeFile.c_str()) == S_OK)
            {
                strExeToRun = strExeFile;
            }
            else
            {
                log::Error(_L_, E_FAIL, L"Executable file %s is not a compatible binary\r\n", strExeFile.c_str());
                return E_FAIL;
            }
        }
        else
        {
            log::Error(_L_, E_FAIL, L"Executable file %s does not exist or is not a file\r\n", strExeRef.c_str());
            return E_FAIL;
        }
    }

    return S_OK;
}

CommandMessage::Message WolfExecution::SetCommandFromConfigItem(const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    if (_wcsicmp(item.strName.c_str(), L"command"))
    {
        log::Verbose(_L_, L"item passed is not a command\r\n");
        return nullptr;
    }

    if (!item[WOLFLAUNCHER_COMMAND_CMDKEYWORD])
    {
        log::Verbose(_L_, L"keyword attribute is missing in command\r\n");
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
                log::Error(_L_, hr, L"Could not get OS version, statement ignored\r\n");
                return nullptr;
            }
            else
            {
                if (bPlus)
                {
                    if (dwWinMajor < dwReqMajor)
                    {
                        log::Verbose(
                            _L_,
                            L"Skipping command %s, OS major version (%d) is incompatible with required %d\r\n",
                            keyword.c_str(),
                            dwWinMajor,
                            dwReqMajor);
                        return nullptr;
                    }
                    if (dwWinMajor == dwReqMajor && dwWinMinor < dwReqMinor)
                    {
                        log::Verbose(
                            _L_,
                            L"Skipping command %s, OS minor version (%d) is incompatible with required %d\r\n",
                            keyword.c_str(),
                            dwWinMinor,
                            dwReqMinor);
                        return nullptr;
                    }
                }
                else if (bMinus)
                {
                    if (dwWinMajor > dwReqMajor)
                    {
                        log::Verbose(
                            _L_,
                            L"Skipping command %s, OS major version (%d) is incompatible with required %d\r\n",
                            keyword.c_str(),
                            dwWinMajor,
                            dwReqMajor);
                        return nullptr;
                    }
                    if (dwWinMajor == dwReqMajor && dwWinMinor > dwReqMinor)
                    {
                        log::Verbose(
                            _L_,
                            L"Skipping command %s, OS minor version (%d) is incompatible with required %d\r\n",
                            keyword.c_str(),
                            dwWinMinor,
                            dwReqMinor);
                        return nullptr;
                    }
                }
                else
                {
                    if (dwWinMajor != dwReqMajor || dwWinMinor != dwReqMinor)
                    {
                        log::Verbose(
                            _L_,
                            L"Skipping command %s, OS version mismatch (%d.%d != required %d.%d%s)\r\n",
                            keyword.c_str(),
                            dwWinMajor,
                            dwWinMinor,
                            dwReqMajor,
                            dwReqMinor);
                        return nullptr;
                    }
                }
                log::Verbose(
                    _L_,
                    L"Command %s, OS version %d.%d compatible with requirement winver=%d.%d%s\r\n",
                    keyword.c_str(),
                    dwWinMajor,
                    dwWinMinor,
                    dwReqMajor,
                    dwReqMinor,
                    bPlus ? L"+" : L"");
            }
        }
        else
        {
            log::Error(_L_, E_INVALIDARG, L"Specified winver attribute (%s) is invalid, ignored\r\n", winver.c_str());
        }
        // No incompatibilities found, proceed with command
    }

    if (item.SubItems[WOLFLAUNCHER_COMMAND_SYSTEMTYPE])
    {
        const wstring& requiredSystemTypes = item[WOLFLAUNCHER_COMMAND_SYSTEMTYPE];
        wstring strProductType;

        if (FAILED(hr = SystemDetails::GetSystemType(strProductType)))
        {
            log::Error(_L_, hr, L"Failed to retrieve system product type\r\n");
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
                log::Verbose(
                    _L_, L"Found product type %s matching system's %s\r\n", tok_iter->c_str(), strProductType.c_str());
                bFound = true;
            }
        }
        if (!bFound)
        {
            log::Verbose(
                _L_,
                L"Command %s will not proceed (%s does not match any of %s\r\n",
                keyword.c_str(),
                strProductType.c_str(),
                requiredSystemTypes.c_str());
            return nullptr;
        }
    }

    auto command = CommandMessage::MakeExecuteMessage(keyword);

    if (item[WOLFLAUNCHER_COMMAND_EXECUTE])
    {
        const wstring& ExeName = item[WOLFLAUNCHER_COMMAND_EXECUTE][WOLFLAUNCHER_EXENAME];

        wstring strExeToRun, strArgToAdd;
        if (FAILED(GetExecutableToRun(item[WOLFLAUNCHER_COMMAND_EXECUTE], strExeToRun, strArgToAdd)))
            return nullptr;

        command->PushExecutable(item[WOLFLAUNCHER_COMMAND_EXECUTE].dwOrderIndex, strExeToRun, ExeName);

        if (!strArgToAdd.empty())
            command->PushArgument(-1, strArgToAdd);
    }
    else
    {
        log::Error(_L_, E_INVALIDARG, L"execute element is missing\r\n");
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
                    log::Error(_L_, E_INVALIDARG, L"The input is missing a name\r\n");
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
                    log::Error(
                        _L_,
                        E_INVALIDARG,
                        L"The input %s is missing a source\r\n",
                        input[WOLFLAUNCHER_INNAME].c_str());
                    hr = E_INVALIDARG;
                    return;
                }
                if (EmbeddedResource::IsResourceBased(input.SubItems[WOLFLAUNCHER_INSOURCE]))
                {
                    const wstring& szResName = input.SubItems[WOLFLAUNCHER_INSOURCE];

                    wstring strModule, strResource, strCabbedName, strFormat;
                    if (FAILED(
                            hr1 = EmbeddedResource::SplitResourceReference(
                                _L_, szResName, strModule, strResource, strCabbedName, strFormat)))
                    {
                        log::Error(_L_, hr1, L"The ressource specified (%s) could not be split\r\n", szResName.c_str());
                        hr = hr1;
                        return;
                    }

                    HMODULE hModule;
                    HRSRC hRes;
                    std::wstring strBinaryPath;
                    if (FAILED(
                            hr1 = EmbeddedResource::LocateResource(
                                _L_, strModule, strResource, EmbeddedResource::BINARY(), hModule, hRes, strBinaryPath)))
                    {
                        log::Error(
                            _L_, hr1, L"The ressource specified (%s) could not be located\r\n", szResName.c_str());
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
                    if (SUCCEEDED(hr1 = GetInputFile(input[WOLFLAUNCHER_INSOURCE].c_str(), strInputFile)))
                    {
                        if (szPattern != NULL)
                            command->PushInputFile(input.dwOrderIndex, strInputFile, strName, szPattern);
                        else
                            command->PushInputFile(input.dwOrderIndex, strInputFile, strName);
                    }
                    else
                    {
                        log::Error(
                            _L_, hr1, L"Input file %s does not exist or is not a file\r\n", strInputFile.c_str());
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
                    log::Info(_L_, L"The output is missing a name\r\n");
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
                    log::Info(
                        _L_, L"The output %s is missing a source\r\n", output[WOLFLAUNCHER_OUTNAME].c_str());
                    hr = E_FAIL;
                    return;
                }
                if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"StdOut"))
                {
                    command->PushStdOut(output.dwOrderIndex, strName, true, 0L, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"StdErr"))
                {
                    command->PushStdErr(output.dwOrderIndex, strName, true, 0L, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"StdOutErr"))
                {
                    command->PushStdOutErr(output.dwOrderIndex, strName, true, 0L, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"File"))
                {
                    if (szPattern == NULL)
                        command->PushOutputFile(output.dwOrderIndex, strName, strName, 0L, true);
                    else
                        command->PushOutputFile(output.dwOrderIndex, strName, strName, szPattern, 0L, true);
                }
                else if (!_wcsicmp(output[WOLFLAUNCHER_OUTSOURCE].c_str(), L"Directory"))
                {
                    if (output[WOLFLAUNCHER_OUTFILEMATCH])
                    {
                        if (szPattern == NULL)
                            command->PushOutputDirectory(
                                output.dwOrderIndex,
                                strName,
                                strName,
                                output[WOLFLAUNCHER_OUTFILEMATCH],
                                0L,
                                true);
                        else
                            command->PushOutputDirectory(
                                output.dwOrderIndex,
                                strName,
                                strName,
                                output[WOLFLAUNCHER_OUTFILEMATCH],
                                szPattern,
                                0L,
                                true);
                    }
                    else
                    {
                        if (szPattern == NULL)
                            command->PushOutputDirectory(output.dwOrderIndex, strName, strName, L"*", 0L, true);
                        else
                            command->PushOutputDirectory(
                                output.dwOrderIndex, strName, strName, L"*", szPattern, 0L, true);
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

    return command;
}

HRESULT WolfExecution::SetRestrictionsFromConfig(const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    if (_wcsicmp(item.strName.c_str(), L"restrictions"))
    {
        log::Verbose(_L_, L"item passed is not restrictions\r\n");
        return E_INVALIDARG;
    }

    WORD arch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(arch)))
    {
        log::Warning(_L_, hr, L"Failed to retrieve architecture");
        return hr;
    }

    if (item[WOLFLAUNCHER_JOBMEMORY])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetFileSizeFromArg(item[WOLFLAUNCHER_JOBMEMORY].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"Specified size is too big for JOB MEMORY restriction (%s)",
                item[WOLFLAUNCHER_JOBMEMORY].c_str());
        }

        if (!m_Restrictions.ExtendedLimits)
        {
            m_Restrictions.ExtendedLimits.emplace();
            ZeroMemory(&m_Restrictions.ExtendedLimits.value(), sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        }
        m_Restrictions.ExtendedLimits->JobMemoryLimit = li.LowPart;
        m_Restrictions.ExtendedLimits->BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
    }

    if (item[WOLFLAUNCHER_PROCESSMEMORY])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetFileSizeFromArg(item[WOLFLAUNCHER_PROCESSMEMORY].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"Specified size is too big for PROCESS memory restriction (%s)",
                item[WOLFLAUNCHER_PROCESSMEMORY].c_str());
        }
        if (!m_Restrictions.ExtendedLimits)
        {
            m_Restrictions.ExtendedLimits.emplace();
            ZeroMemory(&m_Restrictions.ExtendedLimits.value(), sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        }
        m_Restrictions.ExtendedLimits->ProcessMemoryLimit = li.LowPart;
        m_Restrictions.ExtendedLimits->BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    }

    if (item[WOLFLAUNCHER_ELAPSEDTIME])
    {
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(item[WOLFLAUNCHER_ELAPSEDTIME].c_str(), li)))
            return hr;

        if (arch == PROCESSOR_ARCHITECTURE_INTEL && li.QuadPart > MAXDWORD)
        {
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"Specified size is too big for elapsed time restriction (%s)",
                item[WOLFLAUNCHER_ELAPSEDTIME].c_str());
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
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"Specified size is too big for JOB user time restriction (%s)",
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
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"Specified size is too big for PROCESS user time restriction (%s)",
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
        log::Warning(_L_, E_INVALIDARG, L"CpuRate and CpuWeight are mutually exclusive: values ignored\r\n");
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
        if (FAILED(hr = GetIntegerFromArg(item[WOLFLAUNCHER_CPU_RATE].c_str(), weight)))
            return hr;

        if (!m_Restrictions.CpuRateControl)
        {
            m_Restrictions.CpuRateControl.emplace();
            ZeroMemory(&m_Restrictions.CpuRateControl.value(), sizeof(JOBOBJECT_CPU_RATE_CONTROL_INFORMATION));
        }

        if (weight > 9 || weight < 1)
        {
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"CpuWeight valid values are (1-9), current invalid value (%d) is ignored\r\n",
                weight);
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
        log::Verbose(_L_, L"item passed is not a \"command\"\r\n");
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
        log::Verbose(_L_, L"item passed is not a cab item\r\n");
        return E_INVALIDARG;
    }

    if (!item[WOLFLAUNCHER_ARCHIVE_KEYWORD])
    {
        log::Verbose(_L_, L"keyword attribute is missing in archive\r\n");
        return E_INVALIDARG;
    }
    else
        m_strKeyword = item[WOLFLAUNCHER_ARCHIVE_KEYWORD];

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
            m_RepeatBehavior = CreateNew;
        else if (!_wcsicmp(item.c_str(), L"Overwrite"))
            m_RepeatBehavior = Overwrite;
        else if (!_wcsicmp(item.c_str(), L"Once"))
            m_RepeatBehavior = Once;
        else
        {
            log::Error(
                _L_,
                E_INVALIDARG,
                L"Invalid repeat behaviour in config: %s (allowed values are CreateNew, Overwrite, Once)\r\n",
                item.c_str());
            return E_INVALIDARG;
        }
    }
    return S_OK;
}

HRESULT WolfExecution::SetRepeatBehaviour(const Repeat behavior)
{
    if (behavior != NotSet)
        m_RepeatBehavior = behavior;

    if (m_RepeatBehavior == NotSet)
    {
        // Default behaviour is, by default, overwrite
        m_RepeatBehavior = Overwrite;
    }

    return S_OK;
}

HRESULT WolfExecution::SetCompressionLevel(const std::wstring& strCompressionLevel)
{
    if (strCompressionLevel.empty())
        return S_OK;

    m_strCompressionLevel = strCompressionLevel;
    return S_OK;
}
