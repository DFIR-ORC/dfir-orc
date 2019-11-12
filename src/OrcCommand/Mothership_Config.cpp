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
#include "LogFileWriter.h"

#include "ConfigFile_OrcConfig.h"

#include "DownloadTask.h"

#include <sstream>
#include <algorithm>

using namespace std;

using namespace Orc;
using namespace Orc::Command::Mothership;

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
    wstringstream argsBuilder;

    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
                argsBuilder << " " << argv[i];
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
                break;
            case L'-':
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
                    ;
                else
                {
                    PrintUsage();
                    return E_INVALIDARG;
                }
                break;
            default:
                break;
        }
    }
    config.strCmdLineArgs = argsBuilder.str();
    return S_OK;
}

HRESULT Main::GetLocalConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    if (configitem[ORC_DOWNLOAD].Status & ConfigItem::PRESENT)
    {
        m_DownloadTask = DownloadTask::GetTaskFromConfig(_L_, configitem[ORC_DOWNLOAD]);
        if (m_DownloadTask != nullptr)
        {
            config.bUseLocalCopy = true;
        }
    }

    if (configitem[ORC_TEMP].Status & ConfigItem::PRESENT)
    {
        if (FAILED(hr = config.Temporary.Configure(_L_, OutputSpec::Kind::Directory, configitem[ORC_TEMP])))
        {
            log::Warning(
                _L_,
                hr,
                L"Failed to configure temporary folder \"%s\", defaulting to %%TEMP%%\r\n",
                configitem[ORC_TEMP].c_str());
            if (FAILED(hr = config.Temporary.Configure(_L_, OutputSpec::Kind::Directory, L"%TEMP%")))
            {
                log::Warning(_L_, hr, L"Failed to configure temporary folder \"%%TEMP%%\"");
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
        log::Verbose(_L_, L"Looking for reparenting child to %s\r\n", config.strParentName.c_str());
        size_t parentArg = 0;
        if (SUCCEEDED(GetIntegerFromArg(config.strParentName.c_str(), parentArg)))
        {
            // the parent passed is a pid;
            config.dwParentID = (DWORD)parentArg;
        }
        else
        {
            // the parent passed is a process name
            RunningProcesses rp(_L_);

            if (FAILED(hr = rp.EnumProcesses()))
            {
                log::Error(_L_, hr, L"Failed to enumerate running processes, reparenting won't be available\r\n");
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
                log::Verbose(_L_, L"Reparenting child under %s (pid=%d)\r\n", config.strParentName.c_str(), it->m_Pid);
                config.dwParentID = it->m_Pid;
            }
            else
            {
                log::Error(
                    _L_,
                    E_FAIL,
                    L"Desired parent (%s) for reparenting could not be found, reparenting won't happen\r\n");
            }
        }
    }
    else
    {
        log::Verbose(_L_, L"No reparenting for child\r\n");
    }
    return S_OK;
}
