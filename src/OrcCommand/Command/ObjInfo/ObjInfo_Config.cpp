//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ObjInfo.h"

#include "SystemDetails.h"

#include "TableOutputWriter.h"

using namespace Orc;
using namespace Orc::Command::ObjInfo;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return nullptr;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.output.Schema = TableOutput::GetColumnsFromConfig(
        config.output.TableKey.empty() ? L"ObjInfo" : config.output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

    bool bBool = false;
    for (int i = 1; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (OutputOption(argv[i] + 1, L"out", config.output))
                    ;
                else if (ProcessPriorityOption(argv[i] + 1))
                    ;
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    Log::Error(L"Failed to parse command line item: '{}'", argv[i] + 1);
                    PrintUsage();
                    return E_INVALIDARG;
                }
                break;
            default:
                break;
        }
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    if (config.output.Type == OutputSpec::Kind::None)
    {
        config.output.Type = OutputSpec::Kind::TableFile;
        config.output.Path = L"ObjInfo.csv";
        config.output.OutputEncoding = OutputSpec::Encoding::UTF8;
    }

    if (m_outputs.Outputs().empty())
    {
        m_outputs.Outputs().emplace_back(std::make_pair(DirectoryOutput(ObjectDir, L"\\"), nullptr));

        m_outputs.Outputs().emplace_back(std::make_pair(DirectoryOutput(FileDir, L"\\\\.\\pipe\\"), nullptr));
    }
    return S_OK;
}
