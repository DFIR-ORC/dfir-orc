//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ImportData.h"

#include "ParameterCheck.h"
#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include "ConfigFile_ImportData.h"

#include "ImportDefinition.h"

#include "EmbeddedResource.h"
#include "Temporary.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::ImportData;

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.Output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, config.Output.TableKey.empty() ? L"import" : config.Output.TableKey.c_str(), schemaitem);
    return S_OK;
}

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::ImportData::root;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    if (configitem[IMPORTDATA_OUTPUT])
    {
        if (FAILED(hr = config.Output.Configure(_L_, config.Output.supportedTypes, configitem[IMPORTDATA_OUTPUT])))
        {
            return hr;
        }
    }
    if (configitem[IMPORTDATA_IMPORT_OUTPUT])
    {
        if (FAILED(
                hr = config.importOutput.Configure(
                    _L_, config.importOutput.supportedTypes, configitem[IMPORTDATA_IMPORT_OUTPUT])))
        {
            return hr;
        }
    }
    if (configitem[IMPORTDATA_EXTRACT_OUTPUT])
    {
        if (FAILED(
                hr = config.extractOutput.Configure(
                    _L_, config.extractOutput.supportedTypes, configitem[IMPORTDATA_EXTRACT_OUTPUT])))
        {
            return hr;
        }
    }
    if (configitem[IMPORTDATA_RECURSIVE])
    {
        if (!_wcsnicmp(configitem[IMPORTDATA_RECURSIVE].strData.c_str(), L"no", wcslen(L"no")))
        {
            config.bResursive = false;
        }
        else
        {
            config.bResursive = true;
        }
    }

    if (configitem[IMPORTDATA_CONCURRENCY])
    {
        config.dwConcurrency = 0;
        LARGE_INTEGER li;
        if (FAILED(hr = GetIntegerFromArg(configitem[IMPORTDATA_CONCURRENCY].strData.c_str(), li)))
        {
            log::Error(
                _L_,
                hr,
                L"Invalid concurrency value specified (%s), must be an integer.\r\n",
                configitem[IMPORTDATA_CONCURRENCY].strData.c_str());
            return hr;
        }
        if (li.QuadPart > MAXDWORD)
        {
            log::Error(
                _L_,
                hr,
                L"concurrency value specified (%s), must not be insane.\r\n",
                configitem[IMPORTDATA_CONCURRENCY].strData.c_str());
            return hr;
        }
        config.dwConcurrency = li.LowPart;
    }

    if (configitem[IMPORTDATA_TABLE])
    {
        for (auto& table_item : configitem[IMPORTDATA_TABLE].NodeList)
        {
            TableDescription table;

            if (table_item[IMPORTDATA_TABLE_NAME])
            {
                table.Name = table_item[IMPORTDATA_TABLE_NAME];
            }
            if (table_item[IMPORTDATA_TABLE_KEY])
            {
                table.Key = table_item[IMPORTDATA_TABLE_KEY];
            }

            if (table_item[IMPORTDATA_TABLE_SCHEMA])
            {
                table.Schema = table_item[IMPORTDATA_TABLE_SCHEMA];
            }
            if (table_item[IMPORTDATA_TABLE_DISPOSITION])
            {
                using namespace std::string_view_literals;
                if (equalCaseInsensitive((const std::wstring&)table_item[IMPORTDATA_TABLE_DISPOSITION], L"createnew"sv)
                    || equalCaseInsensitive(
                        (const std::wstring&)table_item[IMPORTDATA_TABLE_DISPOSITION], L"create_new"sv))
                    table.Disposition = TableDisposition::CreateNew;
                else if (equalCaseInsensitive(
                             (const std::wstring&)table_item[IMPORTDATA_TABLE_DISPOSITION], L"truncate"sv))
                    table.Disposition = TableDisposition::Truncate;
                else
                    table.Disposition = TableDisposition::AsIs;
            }
            if (table_item[IMPORTDATA_TABLE_COMPRESS])
            {
                if (equalCaseInsensitive((const std::wstring&)table_item[IMPORTDATA_TABLE_COMPRESS], L"no"sv))
                {
                    table.bCompress = false;
                }
                else
                {
                    table.bCompress = true;
                }
            }
            if (table_item[IMPORTDATA_TABLE_TABLOCK])
            {
                if (equalCaseInsensitive((const std::wstring&)table_item[IMPORTDATA_TABLE_TABLOCK], L"no"sv))
                {
                    table.bTABLOCK = false;
                }
                else
                {
                    table.bTABLOCK = true;
                }
            }
            if (table_item[IMPORTDATA_TABLE_CONCURRENCY])
            {
                LARGE_INTEGER li;
                if (FAILED(hr = GetIntegerFromArg(table_item[IMPORTDATA_TABLE_CONCURRENCY].strData.c_str(), li)))
                {
                    log::Error(
                        _L_,
                        hr,
                        L"Invalid concurrency value specified (%s), must be an integer.\r\n",
                        table_item[IMPORTDATA_TABLE_CONCURRENCY].strData.c_str());
                    return hr;
                }
                if (li.QuadPart > MAXDWORD)
                {
                    log::Error(
                        _L_,
                        hr,
                        L"concurrency value specified (%s), must not be insane.\r\n",
                        table_item[IMPORTDATA_TABLE_CONCURRENCY].strData.c_str());
                    return hr;
                }
                table.dwConcurrency = li.LowPart;
            }

            if (table_item[IMPORTDATA_TABLE_BEFORE])
            {
                table.BeforeStatement = table_item[IMPORTDATA_TABLE_BEFORE];
            }
            if (table_item[IMPORTDATA_TABLE_AFTER])
            {
                table.AfterStatement = table_item[IMPORTDATA_TABLE_AFTER];
            }

            config.m_Tables.emplace_back(std::move(table));
        }
    }

    if (configitem[IMPORTDATA_INPUT])
    {
        for (auto& input_item : configitem[IMPORTDATA_INPUT].NodeList)
        {
            Main::InputItem input(_L_);

            if (input_item[IMPORTDATA_INPUT_DIRECTORY])
            {
                input.InputDirectory = input_item[IMPORTDATA_INPUT_DIRECTORY];
            }
            if (input_item[IMPORTDATA_INPUT_MATCH])
            {
                input.NameMatch = input_item[IMPORTDATA_INPUT_MATCH];
            }

            if (input_item[IMPORTDATA_INPUT_BEFORE])
            {
                input.BeforeStatement = input_item[IMPORTDATA_INPUT_BEFORE];
            }
            if (input_item[IMPORTDATA_INPUT_AFTER])
            {
                input.AfterStatement = input_item[IMPORTDATA_INPUT_AFTER].strData;
            }

            if (FAILED(hr = GetDefinitionFromConfig(input_item, input.ImportDefinitions)))
            {
                log::Error(_L_, hr, L"Failed to configure import definitions\r\n");
            }

            config.Inputs.push_back(std::move(input));
        }
    }

    return S_OK;
}

HRESULT Main::GetImportItemFromConfig(const ConfigItem& config_item, ImportDefinition::Item& definition)
{
    if (config_item[IMPORTDATA_INPUT_IMPORT_FILEMATCH])
    {
        definition.NameMatch = config_item[IMPORTDATA_INPUT_IMPORT_FILEMATCH];
    }

    if (config_item[IMPORTDATA_INPUT_IMPORT_BEFORE])
    {
        definition.BeforeStatement = config_item[IMPORTDATA_INPUT_IMPORT_BEFORE];
    }
    if (config_item[IMPORTDATA_INPUT_IMPORT_AFTER])
    {
        definition.AfterStatement = config_item[IMPORTDATA_INPUT_IMPORT_AFTER];
    }

    if (config_item[IMPORTDATA_INPUT_IMPORT_TABLE])
    {
        definition.Table = config_item[IMPORTDATA_INPUT_IMPORT_TABLE];
    }

    if (config_item[IMPORTDATA_INPUT_IMPORT_PASSWORD])
    {
        definition.Password = config_item[IMPORTDATA_INPUT_IMPORT_PASSWORD];
    }
    return S_OK;
}

HRESULT Main::GetIgnoreItemFromConfig(const ConfigItem& config_item, ImportDefinition::Item& import_item)
{
    if (config_item[IMPORTDATA_INPUT_IGNORE_FILEMATCH])
    {
        import_item.NameMatch = config_item[IMPORTDATA_INPUT_IGNORE_FILEMATCH];
    }
    return S_OK;
}

HRESULT Main::GetExtractItemFromConfig(const ConfigItem& config_item, ImportDefinition::Item& import_item)
{
    if (config_item[IMPORTDATA_INPUT_EXTRACT_FILEMATCH])
    {
        import_item.NameMatch = config_item[IMPORTDATA_INPUT_EXTRACT_FILEMATCH];
    }
    if (config_item[IMPORTDATA_INPUT_EXTRACT_PASSWORD])
    {
        import_item.Password = config_item[IMPORTDATA_INPUT_EXTRACT_PASSWORD];
    }
    return S_OK;
}

HRESULT Main::GetDefinitionFromConfig(const ConfigItem& config_item, ImportDefinition& definition)
{
    HRESULT hr = E_FAIL;

    if (config_item[IMPORTDATA_INPUT_IMPORT])
    {

        for (auto& import_item : config_item[IMPORTDATA_INPUT_IMPORT].NodeList)
        {
            ImportDefinition::Item import;

            if (FAILED(hr = GetImportItemFromConfig(import_item, import)))
            {
                log::Error(_L_, hr, L"Failed to get import definition item config\r\n");
            }
            import.ToDo = ImportDefinition::Import;
            definition.m_ItemDefinitions.push_back(std::move(import));
        }
    }
    if (config_item[IMPORTDATA_INPUT_IGNORE])
    {
        for (auto& ignore_item : config_item[IMPORTDATA_INPUT_IGNORE].NodeList)
        {
            ImportDefinition::Item import;

            if (FAILED(hr = GetIgnoreItemFromConfig(ignore_item, import)))
            {
                log::Error(_L_, hr, L"Failed to get import definition item config\r\n");
            }
            import.ToDo = ImportDefinition::Ignore;
            definition.m_ItemDefinitions.push_back(std::move(import));
        }
    }
    if (config_item[IMPORTDATA_INPUT_EXTRACT])
    {
        for (auto& extract_item : config_item[IMPORTDATA_INPUT_EXTRACT].NodeList)
        {
            ImportDefinition::Item import;

            if (FAILED(hr = GetExtractItemFromConfig(extract_item, import)))
            {
                log::Error(_L_, hr, L"Failed to get import definition item config\r\n");
            }
            import.ToDo = ImportDefinition::Extract;
            definition.m_ItemDefinitions.push_back(std::move(import));
        }
    }
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    for (int i = 1; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OutputOption(argv[i] + 1, L"Out", config.Output))
                    ;
                else if (OutputOption(argv[i] + 1, L"Import", config.importOutput))
                    ;
                else if (OutputOption(argv[i] + 1, L"Extract", config.extractOutput))
                    ;
                else if (OutputOption(argv[i] + 1, L"Temp", config.tempOutput))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Recursive", config.bResursive))
                    ;
                else if (OptionalParameterOption(argv[i] + 1, L"Concurrency", config.dwConcurrency))
                    ;
                else if (ProcessPriorityOption(argv[i] + 1))
                    ;
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    PrintUsage();
                    return E_INVALIDARG;
                }
                break;
            default:
            {
                std::wstring strInputDir;
                if (FAILED(hr = GetInputDirectory(argv[i], strInputDir)))
                {
                    std::wstring strInputFile;
                    if (FAILED(hr = GetInputFile(argv[i], strInputFile)))
                    {
                        log::Warning(_L_, hr, L"Invalid input %s specified\r\n", argv[i]);
                    }
                    else
                    {
                        config.strInputFiles.push_back(std::move(strInputFile));
                    }
                }
                else
                {
                    config.strInputDirs.push_back(std::move(strInputDir));
                }
            }
            break;
        }
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;

    if (config.tempOutput.Type == OutputSpec::Kind::None && config.extractOutput.Type == OutputSpec::Kind::Directory)
    {
        config.tempOutput = config.extractOutput;
    }
    else
    {
        log::Verbose(_L_, L"No temporary folder provided, defaulting to %temp%\r\n");
        WCHAR szTempDir[MAX_PATH];
        if (FAILED(hr = UtilGetTempDirPath(szTempDir, MAX_PATH)))
        {
            log::Error(_L_, hr, L"Failed to provide a default temporary folder\r\n");
        }
    }

    if (config.extractOutput.Type == OutputSpec::Kind::None)
    {
        log::Warning(_L_, E_INVALIDARG, L"No extration output directory provided, disabled extraction\r\n");
        config.bDontExtract = true;
    }
    if (config.importOutput.Type == OutputSpec::Kind::None)
    {
        log::Warning(_L_, E_INVALIDARG, L"No import output provided (SQL Connection string), disabled importing\r\n");
        config.bDontImport = true;
    }

    if (config.bDontExtract && config.bDontImport)
    {
        log::Error(_L_, E_INVALIDARG, L"No import nor extract option specified, nothing to do!!\r\n");
        return E_INVALIDARG;
    }

    for (const auto& input : config.Inputs)
    {
        for (const auto& input_def : input.ImportDefinitions.GetDefinitions())
        {
            if (!input_def.Table.empty())
            {
                auto it = find_if(
                    begin(config.m_Tables), end(config.m_Tables), [&input_def](const TableDescription& descr) -> bool {
                        return equalCaseInsensitive(input_def.Table, descr.Name);
                    });
                if (it == end(config.m_Tables))
                {
                    log::Error(
                        _L_,
                        E_INVALIDARG,
                        L"No table description found for table %s (specified in input %s of %s)\r\n",
                        input_def.Table.c_str(),
                        input_def.NameMatch.c_str(),
                        input.NameMatch.c_str());
                    return E_INVALIDARG;
                }
            }
        }
    }

    return S_OK;
}
