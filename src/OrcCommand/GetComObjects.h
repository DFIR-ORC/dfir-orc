//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcCommand.h"

#include "OutputSpec.h"
#include "UtilitiesMain.h"
#include "RegFindConfig.h"
#include "HiveQuery.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

namespace Command::GetComObjects {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration(const logger& pLog)
            : m_RegFindConfig(pLog)
            , m_HiveQuery(pLog)
        {
            m_Output.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::Directory | OutputSpec::Kind::TableFile | OutputSpec::Kind::Archive);
        };

        RegFindConfig m_RegFindConfig;
        HiveQuery m_HiveQuery;
        OutputSpec m_Output;
        std::wstring m_StrComputerName;
    };

public:
    static LPCWSTR ToolName() { return L"GetComObjects"; }
    static LPCWSTR ToolDescription() { return L"GetComObjects - COM Object enumeration and collection"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#GETCOMOBJECTS_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#GETCOMOBJECTS_SQLSCHEMA"; }

    Main(logger pLog)
        : UtilitiesMain(pLog)
        , m_Config(pLog)
    {
    }

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT CheckConfiguration();

    HRESULT GetSchemaFromConfig(const ConfigItem& shemaitem);
    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT Run();

private:
    Configuration m_Config;
    FILETIME m_CollectionDate;
};
}  // namespace Command::GetComObjects
}  // namespace Orc