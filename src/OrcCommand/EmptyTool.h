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

#pragma managed(push, off)

namespace Orc {
class LogFileWriter;

namespace Command::EmptyTool {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        OutputSpec output;

    public:
        Configuration()
        {
            output.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::Directory | OutputSpec::Kind::TableFile | OutputSpec::Kind::Archive);
        };
    };

private:
    Configuration config;

public:
    static LPCWSTR ToolName() { return L"EmptyTool"; }
    static LPCWSTR ToolDescription() { return L"Empty tool (template)"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#EMPTYTOOL_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#EMPTYTOOL_SQLSCHEMA"; }

    Main(logger pLog)
        : UtilitiesMain(std::move(pLog))
    {
    }

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT CheckConfiguration();

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);
    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem) { return S_OK; };  // No Configuration support
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT Run();
};
}  // namespace Command::EmptyTool
}  // namespace Orc
