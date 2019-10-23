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

#include "CryptoHashStream.h"

#include "LocationSet.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

namespace Command::DD {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        std::wstring strIF;
        std::vector<std::wstring> OF;

        CryptoHashStream::Algorithm Hash = CryptoHashStream::Algorithm::Undefined;

        bool NoError = false;
        bool NoTrunc = false;

        ULARGE_INTEGER BlockSize = {512L};
        ULARGE_INTEGER Count = {0L};
        ULARGE_INTEGER Skip = {0L};
        ULARGE_INTEGER Seek = {0L};

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

    LocationSet loc_set;

public:
    static LPCWSTR ToolName() { return L"DD"; }
    static LPCWSTR ToolDescription() { return L"DD - Data Dump"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#DD_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#DD_SQLSCHEMA"; }

    Main(logger pLog)
        : UtilitiesMain(pLog), loc_set(std::move(pLog))
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
    };  // No Local Configuration supprt

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT Run();
};
}  // namespace Command::DD
}  // namespace Orc
