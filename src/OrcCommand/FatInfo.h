//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "OrcCommand.h"
#include "OutputSpec.h"
#include "UtilitiesMain.h"

#include "LocationSet.h"
#include "LocationOutput.h"

#include "TableOutput.h"
#include "TableOutputWriter.h"

#include "Authenticode.h"

#include "FSUtils.h"

#include "boost/logic/tribool.hpp"

#pragma managed(push, off)

namespace Orc {

namespace Command::FatInfo {

class ORCUTILS_API Main : public UtilitiesMain
{
public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
            : locs()
        {
            bResurrectRecords = boost::logic::indeterminate;
            ColumnIntentions = Intentions::FILEINFO_NONE;
            DefaultIntentions = Intentions::FILEINFO_NONE;
            output.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::Directory | OutputSpec::Kind::TableFile | OutputSpec::Kind::Archive
                | OutputSpec::Kind::SQL);
            volumesStatsOutput.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::Directory | OutputSpec::Kind::TableFile | OutputSpec::Kind::Archive
                | OutputSpec::Kind::SQL);
        };

        OutputSpec output;
        OutputSpec volumesStatsOutput;

        LocationSet locs;
        std::wstring strComputerName;


        boost::logic::tribool bResurrectRecords;
        boost::logic::tribool bPopSystemObjects;

        Intentions ColumnIntentions;
        Intentions DefaultIntentions;
        std::vector<Filter> Filters;
    };

    static LPCWSTR ToolName() { return L"FatInfo"; }
    static LPCWSTR ToolDescription() { return L"FAT file system meta data dump"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#FATINFO_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#FATINFO_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , m_Config()
        , m_CodeVerifier()
        , m_FileInfoOutput()
        , m_VolStatOutput()
    {
    }

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);
    HRESULT GetColumnsAndFiltersFromConfig(const ConfigItem& configitems);
    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support
    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();

private:
    boost::logic::tribool GetResurrectFromConfig(const ConfigItem& config);
    boost::logic::tribool GetPopulateSystemObjectsFromConfig(const ConfigItem& config);

private:
    Configuration m_Config;
    Authenticode m_CodeVerifier;
    MultipleOutput<LocationOutput> m_FileInfoOutput;
    MultipleOutput<LocationOutput> m_VolStatOutput;
};
}  // namespace Command::FatInfo
}  // namespace Orc
