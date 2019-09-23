//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "EmptyTool.h"

#include "ConfigFile_EmptyTool.h"

#include "TableOutputWriter.h"

using namespace Orc;
using namespace Orc::Command::EmptyTool;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    // This tool does not have an xml config file
    return Orc::Config::EmptyTool::root;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, config.output.TableKey.empty() ? L"EmptyTable" : config.output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    // Configuration is completed with command line args

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
    // Here we check to tool's provided configuration and adjust (if need be) some parameters to default values

    if (config.output.Type == OutputSpec::Kind::None)
    {
        config.output.Type = OutputSpec::Kind::TableFile;
        config.output.Path = L"EmptyTool.csv";
        config.output.OutputEncoding = OutputSpec::Encoding::UTF8;
    }

    return S_OK;
}
