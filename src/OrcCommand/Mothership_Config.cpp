//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Mothership.h"

#include "RunningProcesses.h"
#include "CaseInsensitive.h"

#include "ConfigFile_OrcConfig.h"

#include "DownloadTask.h"

#include <sstream>
#include <algorithm>

using namespace std;

using namespace Orc;
using namespace Orc::Command::Mothership;

namespace {
void WriteArgument(std::wstringstream& stream, std::wstring_view& arg)
{
    if (arg.find(' ') != std::wstring::npos)
    {
        auto valuePos = arg.find_first_of('=');
        if (valuePos != std::wstring_view::npos)
        {
            valuePos += 1;

            // Add quotes to escape spaces
            const auto variable = arg.substr(0, valuePos);
            const auto value = arg.substr(valuePos, arg.size() - valuePos);

            stream << " " << variable << "\"" << value << "\"";
            return;
        }
    }

    stream << " " << arg;
}
}  // namespace

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return nullptr;
}

ConfigItem::InitFunction Main::GetXmlLocalConfigBuilder()
{
    return Orc::Config::Wolf::Local::root;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    std::wstringstream argsBuilder;

    for (int i = 0; i < argc; i++)
    {
        const wchar_t firstLetter = argv[i][0];
        if (firstLetter == L'/')
        {
            std::wstring_view arg(argv[i]);
            ::WriteArgument(argsBuilder, arg);

            if (ProcessPriorityOption(argv[i] + 1))
            {
                config.dwCreationFlags |= IDLE_PRIORITY_CLASS;
            }
            else if (OutputOption(
                         argv[i] + 1,
                         L"TempDir",
                         static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory),
                         config.Temporary))
            {
            }
        }
        else if (firstLetter == L'-')
        {
            if (BooleanOption(argv[i] + 1, L"NoWait", config.bNoWait))
            {
            }
            else if (BooleanOption(argv[i] + 1, L"WMI", config.bUseWMI))
            {
            }
            else if (BooleanOption(argv[i] + 1, L"PreserveJob", config.bPreserveJob))
            {
            }
            else if (ParameterOption(argv[i] + 1, L"ReParent", config.strParentName))
            {
            }
            else if (ProcessPriorityOption(argv[i] + 1))
            {
                config.dwCreationFlags |= IDLE_PRIORITY_CLASS;
            }
            else if (IgnoreCommonOptions(argv[i] + 1))
            {
            }
            else
            {
                PrintUsage();
                return E_INVALIDARG;
            }
        }
    }

    config.strCmdLineArgs = argsBuilder.str();
    return S_OK;
}

HRESULT Main::GetLocalConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    if (configitem[ORC_DOWNLOAD])
    {
        m_DownloadTask = DownloadTask::GetTaskFromConfig(configitem[ORC_DOWNLOAD]);
        if (m_DownloadTask != nullptr)
        {
            config.bUseLocalCopy = true;
        }
    }

    if (configitem[ORC_TEMP])
    {
        if (FAILED(hr = config.Temporary.Configure(OutputSpec::Kind::Directory, configitem[ORC_TEMP])))
        {
            spdlog::warn(
                L"Failed to configure temporary folder '{}', defaulting to %%TEMP%% (code: {:#x})",
                configitem[ORC_TEMP].c_str(),
                hr);
            if (FAILED(hr = config.Temporary.Configure(OutputSpec::Kind::Directory, L"%TEMP%")))
            {
                spdlog::warn("Failed to configure temporary folder \"%%TEMP%%\" (code: {:#x}", hr);
            }
        }
    }

    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (config.bNoWait)
    {
        if (!config.bUseWMI)
        {
            config.dwCreationFlags |= CREATE_NEW_PROCESS_GROUP;
            config.dwCreationFlags |= DETACHED_PROCESS;
        }
    }

    if (!config.strParentName.empty())
    {
        spdlog::debug(L"Looking for reparenting child to '{}'", config.strParentName);
        size_t parentArg = 0;
        if (SUCCEEDED(GetIntegerFromArg(config.strParentName.c_str(), parentArg)))
        {
            // the parent passed is a pid;
            config.dwParentID = (DWORD)parentArg;
        }
        else
        {
            // the parent passed is a process name
            RunningProcesses rp;

            if (FAILED(hr = rp.EnumProcesses()))
            {
                spdlog::error(L"Failed to enumerate running processes, reparenting won't be available");
                return hr;
            }

            ProcessVector processes;

            rp.GetProcesses(processes);

            auto it = std::find_if(begin(processes), end(processes), [this](const ProcessInfo& pi) {
                if (equalCaseInsensitive(config.strParentName, *pi.strModule))
                    return true;
                return false;
            });

            if (it != end(processes))
            {
                spdlog::debug(L"Reparenting child under '{}' (pid: {})", config.strParentName, it->m_Pid);
                config.dwParentID = it->m_Pid;
            }
            else
            {
                spdlog::error(
                    L"Desired parent '{}' for reparenting could not be found, reparenting won't happen",
                    config.strParentName);
            }
        }
    }
    else
    {
        spdlog::debug("No reparenting for child");
    }
    return S_OK;
}
