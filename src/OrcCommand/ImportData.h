//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcCommand.h"

#include "UtilitiesMain.h"

#include "ImportItem.h"
#include "ImportMessage.h"
#include "ImportNotification.h"
#include "ImportDefinition.h"
#include "ImportAgent.h"

#include "OutputSpec.h"

#include <filesystem>

#pragma managed(push, off)

namespace Orc {

class ImportMessage;
class LogFileWriter;

namespace Command::ImportData {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class InputItem
    {
    private:
        logger _L_;

    public:
        std::wstring matchRegex;  // filename regex
        std::wstring InputDirectory;
        bool bRecursive = false;

        ImportDefinition ImportDefinitions;

        std::wstring BeforeStatement;
        std::wstring AfterStatement;

        std::wstring Description() { return InputDirectory + L"\\" + matchRegex; }

        InputItem(InputItem&& other) noexcept = default;
        InputItem(logger pLog)
            : _L_(pLog)
            , ImportDefinitions(pLog) {};
    };

    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration(logger pLog)
        {
            reportOutput.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::SQL | OutputSpec::Kind::CSV | OutputSpec::Kind::TSV | OutputSpec::Kind::TableFile);
            importOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::SQL);
            extractOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory);
            tempOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory);
        }

        DWORD dwConcurrency = 2;

        bool bRecursive = false;
        bool bDontExtract = false;
        bool bDontImport = false;

        std::vector<std::wstring> strInputDirs;
        std::vector<std::wstring> strInputFiles;

        std::vector<InputItem> Inputs;

        std::vector<TableDescription> m_Tables;

        OutputSpec reportOutput;
        OutputSpec extractOutput;
        OutputSpec importOutput;
        OutputSpec tempOutput;
    };

public:
    static LPCWSTR ToolName() { return L"ImportData"; }
    static LPCWSTR ToolDescription() { return L"ImportData - Import collected data"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#IMPORTDATA_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#IMPORTDATA_SQLSCHEMA"; }
    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);

    Main(logger pLog)
        : UtilitiesMain(pLog)
        , config(pLog)
        , m_importRequestBuffer() {};

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetImportItemFromConfig(const ConfigItem& config_item, ImportDefinition::Item& import_item);
    HRESULT GetExtractItemFromConfig(const ConfigItem& config_item, ImportDefinition::Item& import_item);
    HRESULT GetIgnoreItemFromConfig(const ConfigItem& config_item, ImportDefinition::Item& import_item);

    HRESULT GetDefinitionFromConfig(const ConfigItem& config_item, ImportDefinition& definition);

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();

private:
    Configuration config;

    ImportMessage::PriorityMessageBuffer m_importRequestBuffer;
    std::unique_ptr<Concurrency::call<ImportNotification::Notification>> m_notificationCb;

    // Statictics
    ULONGLONG m_ullProcessedBytes = 0LL;
    ULONGLONG m_ullImportedLines = 0LL;

    HRESULT AddDirectoryForInput(const std::wstring& dir, const InputItem& input, std::vector<ImportItem>& input_paths);
    HRESULT AddFileForInput(const std::wstring& file, const InputItem& input, std::vector<ImportItem>& input_paths);
    std::vector<ImportItem> GetInputFiles(const InputItem& input);
};
}  // namespace Command::ImportData
}  // namespace Orc
