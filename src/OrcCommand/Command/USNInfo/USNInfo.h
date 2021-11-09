//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "UtilitiesMain.h"

#include <optional>

#include <boost/logic/tribool.hpp>

#include "OrcCommand.h"
#include "Location.h"
#include "Configuration/ConfigFile.h"
#include "Location.h"
#include "ParameterCheck.h"

#pragma managed(push, off)

namespace Orc::Command::USNInfo {

class ORCUTILS_API Main : public UtilitiesMain
{
public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
            : locs()
        {
            output.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory | OutputSpec::Kind::Archive);
        };

        OutputSpec output;
        std::wstring strComputerName;

        LocationSet locs;

        bool bCompactForm = false;
        boost::logic::tribool bAddShadows;
        std::optional<LocationSet::ShadowFilters> m_shadows;
        std::optional<LocationSet::PathExcludes> m_excludes;
    };

private:
    class LocationOutput
    {
    public:
        std::shared_ptr<Location> m_pLoc;

        LocationOutput(const std::shared_ptr<Location>& loc)
            : m_pLoc(loc) {};
        LocationOutput(LocationOutput&& other) { std::swap(m_pLoc, other.m_pLoc); };
        LocationOutput(const LocationOutput& other) = default;
        const std::wstring& GetIdentifier()
        {
            static const std::wstring empty;
            if (m_pLoc)
                return m_pLoc->GetIdentifier();
            else
                return empty;
        }
        std::wstring GetLocation()
        {
            static const std::wstring empty;
            if (m_pLoc)
                return m_pLoc->GetLocation();
            else
                return empty;
        }
    };

    MultipleOutput<LocationOutput> m_outputs;

    HRESULT USNRecordInformation(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        WCHAR* szFullName,
        USN_RECORD* pElt);

    Configuration config;

public:
    static LPCWSTR ToolName() { return L"USNInfo"; }
    static LPCWSTR ToolDescription() { return L"USN Journal enumeration"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#USNINFO_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#USNINFO_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , config()
        , m_outputs() {};

    // implemented in USNInfo_Output.cpp
    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);
    HRESULT GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();
};

}  // namespace Orc::Command::USNInfo

#pragma managed(pop)
