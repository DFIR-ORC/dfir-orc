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
#include "Location.h"

#include "ParameterCheck.h"

#include "TaskTracker.h"

#include "GetThis.h"

#pragma managed(push, off)

namespace Orc {

class ConfigFileReader;

namespace Command::GetSamples {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class Configuration : public UtilitiesMain::Configuration
    {

    public:
        OutputSpec getThisConfig;
        OutputSpec samplesOutput;
        OutputSpec sampleinfoOutput;
        OutputSpec timelineOutput;
        OutputSpec autorunsOutput;
        OutputSpec tmpdirOutput;

        std::wstring getthisName;
        std::wstring getthisRef;
        std::wstring getthisArgs;

        LocationSet locs;
        GetThis::Limits limits;

        DWORD dwXOR = 0L;
        OutputSpec::Encoding csvEncoding = OutputSpec::Encoding::UTF8;
        bool bLoadAutoruns = false;
        bool bRunAutoruns = true;
        bool bKeepAutorunsXML = true;
        bool bNoSigCheck = false;
        bool bInstallNTrack = false;
        bool bRemoveNTrack = false;

        Configuration(logger pLog)
            : locs(std::move(pLog))
        {
            getThisConfig.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile);
            samplesOutput.supportedTypes =
                static_cast<OutputSpec::Kind>(OutputSpec::Kind::Archive | OutputSpec::Kind::Directory);
            sampleinfoOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile);
            timelineOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile);
            autorunsOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile);
            tmpdirOutput.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory);
        }
    };

    Configuration config;

private:
    HRESULT LoadAutoRuns(TaskTracker& tk, LPCWSTR szTempDir);

    HRESULT WriteSampleInformation(const std::vector<std::shared_ptr<SampleItem>>& results);
    HRESULT WriteTimeline(const TaskTracker::TimeLine& timeline);

    HRESULT WriteGetThisConfig(
        const std::wstring& strConfigFile,
        const std::vector<std::shared_ptr<Location>>& locs,
        const std::vector<std::shared_ptr<SampleItem>>& results);

    HRESULT RunGetThis(const std::wstring& strConfigFile, LPCWSTR szTempDir);

public:
    static LPCWSTR ToolName() { return L"GetSamples"; }
    static LPCWSTR ToolDescription() { return L"GetSamples - Automate low hanging fruit sample collection"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#GETSAMPLES_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#GETSAMPLES_SQLSCHEMA"; }

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);

    Main(logger pLog)
        : UtilitiesMain(pLog)
        , config(pLog) {};

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);
    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT CheckConfiguration();

    HRESULT Run();

    HRESULT ProcessOptionAutorun(const ConfigItem& item);
};

}  // namespace Command::GetSamples
}  // namespace Orc
