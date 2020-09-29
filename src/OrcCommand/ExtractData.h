//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#pragma once

#include "UtilitiesMain.h"

#include <string>

#include "OrcCommand.h"

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

namespace Command::ExtractData {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class InputItem
    {
    private:
    public:
        std::wstring matchRegex;  // filename regex
        std::wstring path;
        bool bRecursive = false;

        ImportDefinition importDefinitions;

        std::wstring Description() { return path + L"\\" + matchRegex; }

        InputItem() {}
        InputItem(InputItem&& other) noexcept = default;
    };

    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
        {
            reportOutput.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::CSV | OutputSpec::Kind::TSV | OutputSpec::Kind::TableFile);
            output.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory);
            tempOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory);
        }

        DWORD dwConcurrency = 2;  // TODO: configurable

        bool bRecursive = false;

        std::vector<InputItem> inputItems;

        OutputSpec output;
        OutputSpec reportOutput;
        OutputSpec tempOutput;
    };

public:
    static LPCWSTR ToolName() { return L"ExtractData"; }
    static LPCWSTR ToolDescription() { return L"Decrypt, decompress and prepare collected data"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#EXTRACTDATA_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#EXTRACTDATA_REPORT_SQLSCHEMA"; }
    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);

    Main()
        : UtilitiesMain()
        , config()
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
    HRESULT ParseArgument(std::wstring_view argument, Configuration& config);

    Configuration config;

    ImportMessage::PriorityMessageBuffer m_importRequestBuffer;
    std::unique_ptr<Concurrency::call<ImportNotification::Notification>> m_notificationCb;

    // Statictics
    ULONGLONG m_ullProcessedBytes = 0LL;

    HRESULT
    AddDirectoryForInput(const std::filesystem::path& path, const InputItem& inputItem, std::vector<ImportItem>& files);

    HRESULT
    AddFileForInput(const std::filesystem::path& file, const InputItem& inputItem, std::vector<ImportItem>& files);

    std::vector<ImportItem> GetInputFiles(const InputItem& input);
};

}  // namespace Command::ExtractData

}  // namespace Orc
