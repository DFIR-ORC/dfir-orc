//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Robustness.h"
#include "ParameterCheck.h"
#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include "NTFSUtil.h"

using namespace Orc;
using namespace Orc::Command::NTFSUtil;

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return nullptr;
}

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.output.Schema = TableOutput::GetColumnsFromConfig(
        _L_, config.output.TableKey.empty() ? L"Vss" : config.output.TableKey.c_str(), schemaitem);
    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    bool bBool = false;
    for (int i = 1; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (BooleanOption(argv[i] + 1, L"USN", bBool))
                {
                    config.cmd = Main::USN;
                }
                else if (BooleanOption(argv[i] + 1, L"EnumLocs", bBool))
                {
                    config.cmd = Main::EnumLocations;
                }
                else if (BooleanOption(argv[i] + 1, L"MFT", bBool))
                {
                    config.cmd = Main::MFT;
                }
                else if (BooleanOption(argv[i] + 1, L"Find", bBool))
                {
                    config.cmd = Main::Find;
                }
                else if (ParameterOption(argv[i] + 1, L"Record", config.ullRecord))
                {
                    config.cmd = Main::Record;
                }
                else if (BooleanOption(argv[i] + 1, L"HexDump", bBool))
                {
                    config.cmd = Main::Command::HexDump;
                }
                else if (ParameterOption(argv[i] + 1, L"Offset", config.dwlOffset))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Size", config.dwlSize))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Loc", bBool))
                {
                    config.cmd = Main::DetailLocation;
                }
                else if (BooleanOption(argv[i] + 1, L"Configure", config.bConfigure))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"MaxSize", config.dwlMaxSize))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"SizeAtLeast", config.dwlMinSize))
                    ;
                else if (FileSizeOption(argv[i] + 1, L"Delta", config.dwlAllocDelta))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Detail", config.bPrintDetails))
                    ;
                else if (BooleanOption(argv[i] + 1, L"vss", bBool))
                {
                    config.cmd = Main::Command::Vss;
                }
                else if (BooleanOption(argv[i] + 1, L"bitlocker", bBool))
                {
                    config.cmd = Main::Command::BitLocker;
                }
                else if (OutputOption(argv[i] + 1, L"out", config.output))
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
                config.strVolume.assign(argv[i]);
                break;
        }
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    if (config.bConfigure && config.bPrintDetails)
    {
        log::Error(_L_, E_INVALIDARG, L"Cannot configure USN journal AND detail location\r\n");
        return E_INVALIDARG;
    }

    if (config.bConfigure)
    {
        if (config.strVolume.empty() && config.cmd == Main::USN)
        {
            log::Error(_L_, E_INVALIDARG, L"No volume set to be configured\r\n");
            return E_INVALIDARG;
        }
        if (config.dwlMaxSize == 0 && config.dwlMinSize == 0)
        {
            log::Error(_L_, E_INVALIDARG, L"Invalid USN configuration values used (at least minsize or maxsize must be specified)\r\n");
            return E_INVALIDARG;
        }
        else if ((config.dwlMaxSize == 0 || config.dwlMinSize == 0) && config.dwlAllocDelta == 0)
        {
            log::Error(_L_, E_INVALIDARG, L"Invalid USN configuration values used (allocation delta is missing)\r\n");
            return E_INVALIDARG;
        }
    }

    return S_OK;
}
