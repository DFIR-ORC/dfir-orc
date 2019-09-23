//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcCommand.h"

#include <vector>
#include <string>

#include "UtilitiesMain.h"

#include "EmbeddedResource.h"
#include "ConfigFile.h"

#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc::Command::ToolEmbed {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    enum Action
    {
        Embed,
        Dump,
        FromDump
    };

    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Action Todo = Embed;

        std::wstring strInputFile;
        OutputSpec Output;
        std::vector<EmbeddedResource::EmbedSpec> ToEmbed;
    };

private:
    HRESULT
    GetNameValuePairFromConfigItem(const logger& pLog, const ConfigItem& item, EmbeddedResource::EmbedSpec& spec);
    HRESULT GetAddFileFromConfigItem(const logger& pLog, const ConfigItem& item, EmbeddedResource::EmbedSpec& spec);
    HRESULT GetAddArchiveFromConfigItem(const logger& pLog, const ConfigItem& item, EmbeddedResource::EmbedSpec& spec);

    Configuration config;

    HRESULT WriteEmbedConfig(
        const std::wstring& strOutputFile,
        const std::wstring& strMothership,
        const std::vector<EmbeddedResource::EmbedSpec>& values);

    HRESULT Run_Embed();
    HRESULT Run_Dump();
    HRESULT Run_FromDump();

public:
    static LPCWSTR ToolName() { return L"ToolEmbed"; }
    static LPCWSTR ToolDescription() { return L"ToolEmbed - Embed/Extract configuration data and tools"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#TOOLEMBED_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return nullptr; }

    Main(logger pLog)
        : UtilitiesMain(std::move(pLog))
    {
    }

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration supprt

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();
};

}  // namespace Orc::Command::ToolEmbed

#pragma managed(pop)
