//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "DD.h"

#include "ConfigFile_DD.h"

#include "TableOutputWriter.h"

#include "LogFileWriter.h"

using namespace Orc;
using namespace Orc::Command::DD;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    // This tool does not have an xml config file
    return Orc::Config::DD::root;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, config.output.TableKey.empty() ? L"DDTable" : config.output.TableKey.c_str(), schemaitem);
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
                if (OutputOption(
                    argv[i] + 1,
                    L"Out",
                    static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory),
                    config.output))
                    ;
                else if (ParameterOption(argv[i] + 1, L"if", config.strIF))
                    ;
                else if (ParameterListOption(argv[i] + 1, L"of", config.OF, NULL))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"skip", config.Skip.QuadPart))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"seek", config.Seek.QuadPart))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"bs", config.BlockSize.QuadPart))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"count", config.Count.QuadPart))
                    ;
                else if (CryptoHashAlgorithmOption(argv[i] + 1, L"hash", config.Hash))
                    ;
                else if (BooleanOption(argv[i] + 1, L"notrunc", config.NoTrunc))
                    ;
                else if (BooleanOption(argv[i] + 1, L"no_error", config.NoError))
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
    if (config.strIF.empty())
    {
        log::Error(_L_, E_INVALIDARG, L"No if parameter passed\r\n");
        return E_INVALIDARG;
    }
    if (config.OF.empty())
    {
        log::Error(_L_, E_INVALIDARG, L"No if parameter passed\r\n");
        return E_INVALIDARG;
    }

    if (config.output.Type == OutputSpec::Kind::None)
    {
        config.output.Type = OutputSpec::Kind::TableFile;
        config.output.Path = L"DD.csv";
        config.output.OutputEncoding = OutputSpec::Encoding::UTF8;
    }

    if (config.BlockSize.QuadPart == 0)
    {
        config.BlockSize.QuadPart = 512;
    }

    return S_OK;
}
