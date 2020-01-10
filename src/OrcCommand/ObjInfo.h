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

#include "ArchiveMessage.h"
#include "ArchiveNotification.h"
#include "ArchiveAgent.h"

#include "TableOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

namespace Command::ObjInfo {

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

    enum DirectoryType
    {
        ObjectDir,
        FileDir
    };

    class DirectoryOutput
    {
    public:
        DirectoryType m_Type;
        std::wstring m_Directory;

        DirectoryOutput(DirectoryType type, const std::wstring& dir)
            : m_Type(type)
            , m_Directory(dir) {};
        DirectoryOutput(DirectoryOutput&& other)
        {
            std::swap(m_Type, other.m_Type);
            std::swap(m_Directory, other.m_Directory);
        };
        std::wstring GetIdentifier();
    };

    MultipleOutput<DirectoryOutput> m_outputs;

    FILETIME CollectionDate;

public:
    static LPCWSTR ToolName() { return L"ObjInfo"; }
    static LPCWSTR ToolDescription() { return L"Collect meta-data information about NT named objects"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return nullptr; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#OBJINFO_SQLSCHEMA"; }

    Main(logger pLog)
        : UtilitiesMain(pLog)
        , m_outputs(pLog)
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
}  // namespace Command::ObjInfo
}  // namespace Orc
#pragma managed(pop)
