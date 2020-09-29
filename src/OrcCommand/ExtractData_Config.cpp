//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "stdafx.h"

#include "ExtractData.h"

#include "ParameterCheck.h"
#include "TableOutputWriter.h"
#include "ConfigFile_ExtractData.h"
#include "ImportDefinition.h"
#include "EmbeddedResource.h"
#include "Temporary.h"
#include "CaseInsensitive.h"
#include "WinApiHelper.h"
#include "WideAnsi.h"

using namespace Orc;
using namespace Orc::Command::ExtractData;

namespace fs = std::filesystem;

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaItem)
{
    std::wstring tableName(config.reportOutput.TableKey);

    if (tableName.empty())
    {
        tableName = L"extract";
    }

    config.reportOutput.Schema = TableOutput::GetColumnsFromConfig(tableName.c_str(), schemaItem);
    return S_OK;
}

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::ExtractData::root;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configItem)
{
    HRESULT hr = E_FAIL;

    if (configItem[EXTRACTDATA_OUTPUT])
    {
        hr = config.output.Configure(config.output.supportedTypes, configItem[EXTRACTDATA_OUTPUT]);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    if (configItem[EXTRACTDATA_REPORT_OUTPUT])
    {
        hr = config.reportOutput.Configure(config.reportOutput.supportedTypes, configItem[EXTRACTDATA_REPORT_OUTPUT]);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    if (configItem[EXTRACTDATA_RECURSIVE] && equalCaseInsensitive(configItem[EXTRACTDATA_RECURSIVE].strData, L"no"))
    {
        config.bRecursive = false;
    }
    else
    {
        config.bRecursive = true;
    }

    if (configItem[EXTRACTDATA_CONCURRENCY])
    {
        config.dwConcurrency = 0;

        const auto cliArg = configItem[EXTRACTDATA_CONCURRENCY].strData;
        LARGE_INTEGER li;
        hr = GetIntegerFromArg(cliArg.c_str(), li);
        if (FAILED(hr))
        {
            Log::Error(L"Invalid concurrency value specified '{}' must be an integer.", cliArg);
            return hr;
        }

        if (li.QuadPart > MAXDWORD)
        {
            Log::Error(L"concurrency value specified '{}' seems invalid.", cliArg);
            return hr;
        }

        config.dwConcurrency = li.LowPart;
    }

    if (configItem[EXTRACTDATA_INPUT])
    {
        for (const auto& inputNode : configItem[EXTRACTDATA_INPUT].NodeList)
        {
            Main::InputItem inputItem;

            if (inputNode[EXTRACTDATA_INPUT_DIRECTORY])
            {
                inputItem.path = inputNode[EXTRACTDATA_INPUT_DIRECTORY];
            }

            if (inputNode[EXTRACTDATA_INPUT_MATCH])
            {
                inputItem.matchRegex = inputNode[EXTRACTDATA_INPUT_MATCH];
            }

            hr = GetDefinitionFromConfig(inputNode, inputItem.importDefinitions);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to configure import definitions");
            }

            config.inputItems.push_back(std::move(inputItem));
        }
    }

    return S_OK;
}

HRESULT Main::GetImportItemFromConfig(const ConfigItem& configItem, ImportDefinition::Item& definition)
{
    if (configItem[EXTRACTDATA_INPUT_EXTRACT_FILEMATCH])
    {
        definition.nameMatch = configItem[EXTRACTDATA_INPUT_EXTRACT_FILEMATCH];
    }

    if (configItem[EXTRACTDATA_INPUT_EXTRACT_PASSWORD])
    {
        definition.Password = configItem[EXTRACTDATA_INPUT_EXTRACT_PASSWORD];
    }

    return S_OK;
}

HRESULT Main::GetIgnoreItemFromConfig(const ConfigItem& configItem, ImportDefinition::Item& importItem)
{
    if (configItem[EXTRACTDATA_INPUT_IGNORE_FILEMATCH])
    {
        importItem.nameMatch = configItem[EXTRACTDATA_INPUT_IGNORE_FILEMATCH];
    }

    return S_OK;
}

HRESULT Main::GetDefinitionFromConfig(const ConfigItem& configItem, ImportDefinition& definition)
{
    HRESULT hr = E_FAIL;

    if (configItem[EXTRACTDATA_INPUT_IGNORE])
    {
        for (const auto& ignoreItem : configItem[EXTRACTDATA_INPUT_IGNORE].NodeList)
        {
            ImportDefinition::Item import;
            hr = GetIgnoreItemFromConfig(ignoreItem, import);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to get import definition item config");
            }

            import.ToDo = ImportDefinition::Ignore;
            definition.m_itemDefinitions.push_back(std::move(import));
        }
    }

    if (configItem[EXTRACTDATA_INPUT_EXTRACT])
    {
        for (const auto& extractItem : configItem[EXTRACTDATA_INPUT_EXTRACT].NodeList)
        {
            ImportDefinition::Item import;
            hr = GetImportItemFromConfig(extractItem, import);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to get import definition item config");
            }

            import.ToDo = ImportDefinition::Extract;
            definition.m_itemDefinitions.push_back(std::move(import));
        }
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    for (int i = 1; i < argc; i++)
    {
        ParseArgument(argv[i], config);
    }

    return S_OK;
}

HRESULT Main::ParseArgument(std::wstring_view arg, Configuration& config)
{
    if (arg.size() < 2)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = E_FAIL;
    switch (arg[0])
    {
        case L'/':
        case L'-':
            if (OutputOption(arg.data() + 1, L"Out", OutputSpec::Kind::Directory, config.output))
                break;

            if (OutputOption(arg.data() + 1, L"Report", OutputSpec::Kind::TableFile, config.reportOutput))
                break;

            if (OutputOption(arg.data() + 1, L"Temp", OutputSpec::Kind::Directory, config.tempOutput))
                break;

            if (BooleanOption(arg.data() + 1, L"Recursive", config.bRecursive))
                break;

            if (OptionalParameterOption(arg.data() + 1, L"Concurrency", config.dwConcurrency))
                break;

            if (ProcessPriorityOption(arg.data() + 1))
                break;

            if (UsageOption(arg.data() + 1))
                break;

            if (IgnoreCommonOptions(arg.data() + 1))
                break;

            PrintUsage();
            return E_INVALIDARG;

        default: {
            std::error_code ec;
            auto path = ExpandEnvironmentStringsApi(arg.data(), ec);
            if (ec)
            {
                Log::Warn(L"Invalid input '{}' specified (code: {:#x})", arg.data(), ec.value());
                return HRESULT_FROM_WIN32(ec.value());
            }

            Main::InputItem inputItem;
            inputItem.path = path;
            inputItem.matchRegex = L".*";  // regex filter for "on disk" files to extract

            // match filter for "in archive" files to ignore
            // ImportDefinition::Item ignoreFilter;
            // ignoreFilter.nameMatch = L"";  // wildcard filter for "in archive" files to ignore
            // ignoreFilter.ToDo = ImportDefinition::Ignore;
            // inputItem.importDefinitions.m_itemDefinitions.push_back(std::move(ignoreFilter));

            // match filter for "in archive" files to extract
            ImportDefinition::Item extractFilter;
            extractFilter.nameMatch = L"*";  // wildcard filter for "in archive" files to extract
            extractFilter.ToDo = ImportDefinition::Extract;
            extractFilter.Password = L"";

            inputItem.importDefinitions.m_itemDefinitions.push_back(std::move(extractFilter));

            // config.inputDirs.push_back(std::move(path));
            config.inputItems.push_back(std::move(inputItem));

            return S_OK;
        }
        break;
    }

    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (config.output.Type == OutputSpec::Kind::None)
    {
        std::error_code ec;
        const auto workingDir = GetWorkingDirectoryApi(ec);
        if (ec)
        {
            Log::Error("Failed GetWorkingDirectory (code: {:#x})", ec.value());
            return HRESULT_FROM_WIN32(ec.value());
        }

        config.output.Configure(OutputSpec::Kind::Directory, workingDir.c_str());
    }

    if (config.tempOutput.Type == OutputSpec::Kind::None)
    {
        config.tempOutput = config.output;
    }
    else
    {
        Log::Debug("No temporary folder provided, defaulting to %TEMP%");

        WCHAR szTempDir[MAX_PATH];
        hr = UtilGetTempDirPath(szTempDir, MAX_PATH);
        if (FAILED(hr))
        {
            Log::Error("Failed to provide a default temporary folder (code: {:#x})", hr);
        }
    }

    return S_OK;
}
