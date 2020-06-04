//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WolfLauncher.h"

#include "ConfigFileReader.h"
#include "ConfigFileWriter.h"
#include "TemporaryStream.h"
#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include "LocationSet.h"

#include "CaseInsensitive.h"

#include "SystemDetails.h"

#include "ConfigFile_WOLFLauncher.h"
#include "ConfigFile_OrcConfig.h"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/tokenizer.hpp>

#include <filesystem>

using namespace std;

using namespace Orc;
using namespace Orc::Command::Wolf;

HRESULT Main::GetSchemaFromConfig(const ConfigItem& schemaitem)
{
    config.JobStatistics.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.JobStatistics.TableKey.empty() ? L"JobStatistics" : config.JobStatistics.TableKey.c_str(),
        schemaitem);
    config.ProcessStatistics.Schema = TableOutput::GetColumnsFromConfig(
        _L_,
        config.ProcessStatistics.TableKey.empty() ? L"ProcessStatistics" : config.ProcessStatistics.TableKey.c_str(),
        schemaitem);
    return S_OK;
}

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::Wolf::root;
}

ConfigItem::InitFunction Main::GetXmlLocalConfigBuilder()
{
    return Orc::Config::Wolf::Local::root;
}

std::shared_ptr<WolfExecution::Recipient> Main::GetRecipientFromItem(const ConfigItem& recipient_item)
{
    HRESULT hr = E_FAIL;

    auto retval = std::make_shared<WolfExecution::Recipient>();

    if (recipient_item[WOLFLAUNCHER_RECIPIENT_NAME])
    {
        retval->Name = recipient_item[WOLFLAUNCHER_RECIPIENT_NAME];
        auto inlist = GetRecipient(retval->Name);
        if (inlist != nullptr)
        {
            log::Error(_L_, E_INVALIDARG, L"Only one recipient with name %s is allowed.\r\n", retval->Name.c_str());
            return nullptr;
        }
    }
    else
    {
        log::Error(_L_, E_INVALIDARG, L"Recipient name attribute is mandatory.\r\n");
        return nullptr;
    }

    if (recipient_item[WOLFLAUNCHER_RECIPIENT_ARCHIVE])
    {
        boost::split(
            retval->ArchiveSpec, (std::wstring_view) recipient_item[WOLFLAUNCHER_RECIPIENT_ARCHIVE], boost::is_any_of(",;"));
    }
    else
    {
        log::Error(_L_, E_INVALIDARG, L"Recipient archive attribute is mandatory.\r\n");
        return nullptr;
    }

    DWORD cbOutput = 0L;
    DWORD dwFormatFlags = 0L;

    const std::wstring& strCert = recipient_item;

    if (!CryptStringToBinaryW(
            strCert.c_str(), (DWORD)strCert.size(), CRYPT_STRING_ANY, NULL, &cbOutput, NULL, &dwFormatFlags))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"CryptStringToBinary failed to convert %.20s into a binary blob\r\n",
            strCert.c_str());
        return nullptr;
    }

    CBinaryBuffer pCertBin;
    pCertBin.SetCount(cbOutput);
    if (!CryptStringToBinaryW(
            strCert.c_str(),
            (DWORD)strCert.size(),
            CRYPT_STRING_ANY,
            pCertBin.GetData(),
            &cbOutput,
            NULL,
            &dwFormatFlags))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"CryptStringToBinary failed to convert %.20s into a binary blob\r\n",
            strCert.c_str());
        return nullptr;
    }

    std::swap(retval->Certificate, pCertBin);

    return retval;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    using namespace std::string_view_literals;

    HRESULT hr = E_FAIL;

    ConfigFile reader(_L_);

    if (config.bAddConfigToArchive)
    {
        auto stream = std::make_shared<TemporaryStream>(_L_);

        if (SUCCEEDED(hr = stream->Open(config.TempWorkingDir.Path, L"WolfConfigStream", 100 * 1024)))
        {
            ConfigFileWriter w(_L_);

            if (SUCCEEDED(w.WriteConfig(stream, L"DFIR-ORC Configuration", configitem)))
            {
                m_pConfigStream = stream;
            }
        }
    }

    config.bChildDebug = boost::indeterminate;
    if (configitem[WOLFLAUNCHER_CHILDDEBUG])
    {
        if (equalCaseInsensitive(L"No"sv, (const std::wstring&)configitem[WOLFLAUNCHER_CHILDDEBUG]))
            config.bChildDebug = false;
        else
            config.bChildDebug = true;
    }

    if (configitem[WOLFLAUNCHER_WERDONTSHOWUI])
    {
        if (equalCaseInsensitive(L"No"sv, (const std::wstring&)configitem[WOLFLAUNCHER_WERDONTSHOWUI]))
            config.bWERDontShowUI = false;
        else
            config.bWERDontShowUI = true;
    }

    if (configitem[WOLFLAUNCHER_LOG])
    {
        auto hr = E_FAIL;
        if (FAILED(hr = config.Log.Configure(_L_, configitem[WOLFLAUNCHER_LOG])))
        {
            log::Warning(_L_, hr, L"Failed to configure DFIR-Orc log file\r\n");
        }
    }

    if (configitem[WOLFLAUNCHER_OUTLINE])
    {
        auto hr = E_FAIL;
        if (FAILED(hr = config.Outline.Configure(_L_, configitem[WOLFLAUNCHER_OUTLINE])))
        {
            log::Warning(_L_, hr, L"Failed to configure DFIR-Orc outline file\r\n");
        }
    }

    if (!configitem[WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT])
    {
        config.msCommandTerminationTimeOut = 3h;  // 3 hours
    }
    else
    {
        DWORD dwCommandTimeOutInMinutes = 0L;
        if (FAILED(
                hr = GetIntegerFromArg(
                    configitem[WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT].c_str(), dwCommandTimeOutInMinutes)))
        {
            log::Error(
                _L_,
                hr,
                L"Invalid command timeout (command_timeout) value specified (%s), must be an integer.\r\n",
                configitem[WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT].c_str());
            return hr;
        }
        config.msCommandTerminationTimeOut = std::chrono::minutes(dwCommandTimeOutInMinutes);
    }

    if (!configitem[WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT])
    {
        config.msArchiveTimeOut = 5min;  // 5 minutes
    }
    else
    {
        DWORD dwArchiveTimeOutMinutes = 0L;
        if (FAILED(
                hr = GetIntegerFromArg(
                    configitem[WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT].c_str(), dwArchiveTimeOutMinutes)))
        {
            log::Error(
                _L_,
                hr,
                L"Invalid archive timeout (archive_timeout) specified (%s), must be an integer.\r\n",
                configitem[WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT].c_str());
            return hr;
        }
        config.msArchiveTimeOut = std::chrono::minutes(dwArchiveTimeOutMinutes);
    }

    if (configitem[WOLFLAUNCHER_RECIPIENT])
    {

        for (auto& recipient_item : configitem[WOLFLAUNCHER_RECIPIENT].NodeList)
        {

            auto recipient = GetRecipientFromItem(recipient_item);

            if (recipient != nullptr)
                m_Recipients.push_back(recipient);
        }
    }

    for (const ConfigItem& archiveitem : configitem[WOLFLAUNCHER_ARCHIVE].NodeList)
    {

        auto exec = std::make_unique<WolfExecution>(_L_);

        if (FAILED(hr = exec->SetJobConfigFromConfig(archiveitem)))
        {
            log::Error(_L_, hr, L"Job configuration failed\r\n");
            return hr;
        }

        if (FAILED(
                hr = exec->SetJobTimeOutFromConfig(
                    archiveitem, config.msCommandTerminationTimeOut, config.msArchiveTimeOut)))
        {
            log::Error(_L_, hr, L"Job timeout configuration failed\r\n");
            return hr;
        }

        if (FAILED(hr = exec->SetRestrictionsFromConfig(archiveitem.SubItems[WOLFLAUNCHER_ARCHIVE_RESTRICTIONS])))
        {
            log::Error(_L_, hr, L"Command restrictions setup failed\r\n");
            return hr;
        }

        if (FAILED(hr = exec->SetRepeatBehaviourFromConfig(archiveitem.SubItems[WOLFLAUNCHER_ARCHIVE_REPEAT])))
        {
            log::Error(_L_, hr, L"Command repeat behavior setup failed\r\n");
            return hr;
        }

        if (FAILED(hr = exec->SetCommandsFromConfig(archiveitem.SubItems[WOLFLAUNCHER_ARCHIVE_COMMAND])))
        {
            log::Error(_L_, hr, L"Command creation failed\r\n");
            return hr;
        }

        if (FAILED(hr = exec->SetArchiveName((const std::wstring&)archiveitem[WOLFLAUNCHER_ARCHIVE_NAME])))
        {
            log::Error(
                _L_, hr, L"Failed to set %s as cab name\r\n", archiveitem[WOLFLAUNCHER_ARCHIVE_NAME].c_str());
            return hr;
        }

        if (FAILED(hr = exec->SetConfigStreams(m_pConfigStream, m_pLocalConfigStream)))
        {
            log::Error(
                _L_, hr, L"Failed to set config stream\r\n", archiveitem[WOLFLAUNCHER_ARCHIVE_NAME].c_str());
            return hr;
        }

        if (archiveitem[WOLFLAUNCHER_ARCHIVE_OPTIONAL])
        {
            if (!_wcsicmp(archiveitem[WOLFLAUNCHER_ARCHIVE_OPTIONAL].c_str(), L"no"))
            {
                exec->SetMandatory();
            }
            else
            {
                exec->SetOptional();
            }
        }

        if (archiveitem[WOLFLAUNCHER_ARCHIVE_CHILDDEBUG])
        {
            if (!_wcsicmp(archiveitem[WOLFLAUNCHER_ARCHIVE_CHILDDEBUG].c_str(), L"no"))
            {
                exec->UnSetChildDebug();
            }
            else
            {
                exec->SetChildDebug();
            }
        }

        m_wolfexecs.push_back(std::move(exec));
    }

    return S_OK;
}

HRESULT Main::GetLocalConfigurationFromConfig(const ConfigItem& configitem)
{
    wstring strOutputDir;

    if (config.bAddConfigToArchive && configitem)
    {
        auto stream = std::make_shared<TemporaryStream>(_L_);

        if (auto hr = stream->Open(config.TempWorkingDir.Path, L"DFIROrcConfigStream", 100 * 1024); FAILED(hr))
            return hr;

        ConfigFileWriter w(_L_);

        if (auto hr = w.WriteConfig(stream, L"DFIR-ORC Local Configuration", configitem); FAILED(hr))
            return hr;

        m_pLocalConfigStream = stream;
    }

    if (auto hr = config.Output.Configure(
                _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory), configitem[ORC_OUTPUT]); FAILED(hr))
    {
        log::Error(_L_, hr, L"Error in specified outputdir in config file, defaulting to .\r\n");
        return hr;
    }

    if (configitem[ORC_UPLOAD])
    {
        auto upload = std::make_shared<OutputSpec::Upload>();

        if (upload == nullptr)
            return E_OUTOFMEMORY;

        if (auto hr = upload->Configure(_L_, configitem[ORC_UPLOAD]); FAILED(hr))
        {
            log::Error(_L_, hr, L"Error in specified upload section in config file, ignored\r\n");
            return hr;
        }
        config.Output.UploadOutput = upload;
    }

    if (auto hr = config.TempWorkingDir.Configure(
                _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory), configitem[ORC_TEMP]); FAILED(hr))
    {
        log::Error(_L_, hr, L"Error in specified tempdir in config file, defaulting to current directory\r\n");
        config.TempWorkingDir.Path = L".\\DFIR-OrcTempDir";
        config.TempWorkingDir.Type = OutputSpec::Kind::Directory;
    }

    if (configitem[ORC_RECIPIENT])
    {
        for (auto& recipient_item : configitem[ORC_RECIPIENT].NodeList)
        {
            auto recipient = GetRecipientFromItem(recipient_item);

            if (recipient == nullptr)
                return E_FAIL;

            m_Recipients.push_back(recipient);
        }
    }
    if (configitem[ORC_PRIORITY])
    {
        if (!_wcsicmp(L"Normal", configitem[ORC_PRIORITY].c_str()))
            config.Priority = Normal;
        else if (!_wcsicmp(L"Low", configitem[ORC_PRIORITY].c_str()))
            config.Priority = Low;
        else if (!_wcsicmp(L"High", configitem[ORC_PRIORITY].c_str()))
            config.Priority = High;
    }

    if (configitem[ORC_POWERSTATE])
    {
        std::set<wstring> powerstates;

        CSVListToContainer(configitem[ORC_POWERSTATE], config.strPowerStates);
    }

    if (configitem[ORC_ALTITUDE])
    {
        config.DefaultAltitude = LocationSet::GetAltitudeFromString(configitem[ORC_ALTITUDE].c_str());
    }

    if (configitem[ORC_KEY])
    {
        for (const auto& item : configitem[ORC_KEY].NodeList)
        {
            CSVListToContainer(item, config.OnlyTheseKeywords);
        }
    }

    if (configitem[ORC_ENABLE_KEY])
    {
        for (const auto& item : configitem[ORC_ENABLE_KEY].NodeList)
        {
            CSVListToContainer(item, config.EnableKeywords);
        }
    }

    if (configitem[ORC_DISABLE_KEY])
    {
        for (const auto& item : configitem[ORC_DISABLE_KEY].NodeList)
        {
            CSVListToContainer(item, config.DisableKeywords);
        }
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    try
    {
        bool bExecute = false;
        bool bKeywords = false;
        bool bDump = false;
        bool bFromDump = false;

        std::wstring strComputerName;
        std::wstring strFullComputerName;
        std::wstring strSystemType;
        std::wstring strTags;

        for (int i = 0; i < argc; i++)
        {
            std::wstring strPriority;

            switch (argv[i][0])
            {
                case L'/':
                case L'-':
                    if (OutputOption(
                            argv[i] + 1,
                            L"Out",
                            static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory),
                            config.Output))
                        ;
                    else if (OutputOption(
                                 argv[i] + 1,
                                 L"TempDir",
                                 static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory),
                                 config.TempWorkingDir))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Keys", bKeywords))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Execute", bExecute))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Dump", bDump))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"FromDump", bFromDump))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"NoChildDebug", config.bChildDebug))
                        config.bChildDebug = !config.bChildDebug;
                    else if (BooleanOption(argv[i] + 1, L"ChildDebug", config.bChildDebug))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Once", config.bRepeatOnce))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Overwrite", config.bRepeatOverwrite))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"CreateNew", config.bRepeatCreateNew))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Compression", config.strCompressionLevel))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"archive_timeout", config.msArchiveTimeOut))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"command_timeout", config.msCommandTerminationTimeOut))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"key-", config.DisableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"-key", config.DisableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"key+", config.EnableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"+key", config.EnableKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"key", config.OnlyTheseKeywords, L","))
                        ;
                    else if (ParameterListOption(argv[i] + 1, L"power", config.strPowerStates, L","))
                        ;
                    else if (AltitudeOption(argv[i] + 1, L"Altitude", config.DefaultAltitude))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Computer", strComputerName))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"FullComputer", strFullComputerName))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"SystemType", strSystemType))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Tags", strTags))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Offline", config.strOfflineLocation))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"Beep", config.bBeepWhenDone))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"Priority", strPriority))
                    {
                        if (!_wcsicmp(L"Normal", strPriority.c_str()))
                            config.Priority = Normal;
                        else if (!_wcsicmp(L"Low", strPriority.c_str()))
                            config.Priority = Low;
                        else if (!_wcsicmp(L"High", strPriority.c_str()))
                            config.Priority = High;
                    }
                    else if (BooleanOption(argv[i] + 1, L"WERDontShowUI", config.bWERDontShowUI))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"tee_cleartext", config.bTeeClearTextOutput))
                        ;
                    else if (BooleanOption(argv[i] + 1, L"no_journaling", config.bNoJournaling))
                    {
                        config.bUseJournalWhenEncrypting = false;
                    }
                    else if (EncodingOption(argv[i] + 1, config.Output.OutputEncoding))
                    {
                        config.TempWorkingDir.OutputEncoding = config.Output.OutputEncoding;
                    }
                    else if (WaitForDebuggerOption(argv[i] + 1))
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

        if (strComputerName.empty() && !strFullComputerName.empty())
        {
            strComputerName = strFullComputerName;
        }
        if (strFullComputerName.empty() && !strComputerName.empty())
        {
            strFullComputerName = strComputerName;
        }
        if (!strComputerName.empty())
            SystemDetails::SetOrcComputerName(strComputerName);
        if (!strFullComputerName.empty())
            SystemDetails::SetOrcFullComputerName(strFullComputerName);
        if (!strSystemType.empty())
            SystemDetails::SetSystemType(strSystemType);
        if (!strTags.empty())
        {
            using tokenizer =
                boost::tokenizer<boost::char_separator<wchar_t>, std::wstring::const_iterator, std::wstring>;

            tokenizer tokens(strTags, boost::char_separator<wchar_t>(L","));

            SystemTags tags;
            for (const auto& tag : tokens)
            {
                tags.insert(tag);
            }
            SystemDetails::SetSystemTags(std::move(tags));
        }

        config.SelectedAction = WolfLauncherAction::Execute;
        if (bExecute)
            config.SelectedAction = WolfLauncherAction::Execute;
        else if (bKeywords)
            config.SelectedAction = WolfLauncherAction::Keywords;
        else if (bDump)
            config.SelectedAction = WolfLauncherAction::Dump;
        else if (bFromDump)
            config.SelectedAction = WolfLauncherAction::FromDump;
    }
    catch (...)
    {
        log::Info(_L_, L"WolfLauncher failed during argument parsing, exiting\r\n");
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;
    if ((config.bRepeatCreateNew ? 1 : 0) + (config.bRepeatOnce ? 1 : 0) + (config.bRepeatOverwrite ? 1 : 0) > 1)
    {
        log::Error(_L_, E_FAIL, L"options /createnew, /once and /overwrite are mutually exclusive\r\n");
        return E_FAIL;
    }

    if (config.Output.Type == OutputSpec::Kind::None)
    {
        config.Output.OutputEncoding = OutputSpec::Encoding::UTF8;
        config.Output.Type = OutputSpec::Kind::Directory;
        config.Output.Path = L".";
    }

    if (config.Output.Type == OutputSpec::Kind::Directory)
    {
        DWORD dwLenRequired = GetFullPathName(config.Output.Path.c_str(), 0L, NULL, NULL);
        if (dwLenRequired == 0L)
        {
            log::Error(_L_, E_INVALIDARG, L"Output path \"%s\" was not convertible to an absolute path\r\n");
            return E_INVALIDARG;
        }

        CBinaryBuffer buffer;
        buffer.SetCount(dwLenRequired * sizeof(WCHAR));

        if (GetFullPathName(config.Output.Path.c_str(), dwLenRequired, buffer.GetP<WCHAR>(0), NULL) == 0)
        {
            log::Error(_L_, E_INVALIDARG, L"Failed to convert \"%s\" to an absolute path\r\n");
            return E_INVALIDARG;
        }

        config.Output.Path.assign(buffer.GetP<WCHAR>(0), dwLenRequired - 1);  // remove trailing \0
    }

    if (config.Output.Type != OutputSpec::Kind::Directory)
    {
        log::Error(_L_, E_INVALIDARG, L"Invalid output specification");
        return E_INVALIDARG;
    }

    if (config.TempWorkingDir.Type == OutputSpec::Kind::None)
    {
        if (FAILED(
                hr = config.TempWorkingDir.Configure(
                    _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::Directory), L"%TEMP%\\WorkingTemp")))
        {
            log::Error(_L_, hr, L"Failed to use default temporary directory from %TEMP%\r\n");
            return hr;
        }
    }
    if (config.TempWorkingDir.Type != OutputSpec::Kind::Directory)
    {
        log::Error(_L_, E_INVALIDARG, L"Invalid temporary location specification");
        return E_INVALIDARG;
    }

    if (config.Log.Type != OutputSpec::Kind::None)
    {
        if (config.Log.IsFile())
        {
            // We need to apply the output directory path to the log file
            config.Log.Path = std::filesystem::path(config.Output.Path) / config.Log.FileName;
        }
        else
        {
            log::Error(_L_, E_INVALIDARG, L"Invalid log file type");
            return E_INVALIDARG;
        }
    }

    if (config.Outline.Type != OutputSpec::Kind::None)
    {
        if (config.Outline.IsStructuredFile())
        {
            // We need to apply the output directory path to the outline file
            config.Outline.Path = std::filesystem::path(config.Output.Path) / config.Outline.FileName;
        }
        else
        {
            log::Error(_L_, E_INVALIDARG, L"Invalid outline file type");
            return E_INVALIDARG;
        }
    }


    if (config.JobStatistics.Type == OutputSpec::Kind::None)
    {
        if (FAILED(
                hr = config.JobStatistics.Configure(
                    _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile), L"JobStatistics.csv")))
        {
            log::Error(_L_, hr, L"Failed to set job statistics output\r\n");
            return hr;
        }
    }

    if (config.ProcessStatistics.Type == OutputSpec::Kind::None)
    {
        if (FAILED(
                hr = config.ProcessStatistics.Configure(
                    _L_, static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile), L"ProcessStatistics.csv")))
        {
            log::Error(_L_, hr, L"Failed to set process statistics output\r\n");
            return hr;
        }
    }

    if (config.bRepeatCreateNew)
        config.RepeatBehavior = WolfExecution::CreateNew;
    if (config.bRepeatOnce)
        config.RepeatBehavior = WolfExecution::Once;
    if (config.bRepeatOverwrite)
        config.RepeatBehavior = WolfExecution::Overwrite;

    for (const auto& wolfexec : m_wolfexecs)
    {

        HRESULT hr = E_FAIL;
        wolfexec->SetRepeatBehaviour(config.RepeatBehavior);
        wolfexec->SetOutput(config.Output, config.TempWorkingDir, config.JobStatistics, config.ProcessStatistics);

        wolfexec->SetRecipients(m_Recipients);

        if (!config.strCompressionLevel.empty())
            wolfexec->SetCompressionLevel(config.strCompressionLevel);

        if (FAILED(hr = wolfexec->BuildFullArchiveName()))
        {
            log::Error(_L_, hr, L"Failed to build full archive name for %s\r\n", wolfexec->GetKeyword().c_str());
        }
    }

    for (const auto& strPowerState : config.strPowerStates)
    {
        if (!_wcsicmp(L"SystemRequired", strPowerState.c_str()))
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | SystemRequired);
        else if (!_wcsicmp(L"DisplayRequired", strPowerState.c_str()))
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | DisplayRequired);
        else if (!_wcsicmp(L"UserPresent", strPowerState.c_str()))
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | UserPresent);
        else if (!_wcsicmp(L"AwayMode", strPowerState.c_str()))
            config.PowerState = static_cast<WolfPowerState>(config.PowerState | AwayMode);
        else
        {
            log::Warning(
                _L_,
                HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                L"Invalid powerstate value %s: ignored\r\n",
                strPowerState.c_str());
        }
    }

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    if (dwMajor < 6)
    {
        config.bChildDebug = false;  // On XP (prior to Vista), using a dbghelp debugger fails
    }

    if (config.strOfflineLocation.has_value())
    {
        // DFIR-Orc is now in Offline mode (to handle disk images)

        // First, we check that dfir-orc computer & system type have been set
        std::wstring strFullComputerName;
        SystemDetails::GetFullComputerName(strFullComputerName);
        std::wstring strOrcFullComputerName;
        SystemDetails::GetOrcFullComputerName(strOrcFullComputerName);

        if (strFullComputerName == strOrcFullComputerName)
        {
            log::Warning(
                _L_,
                E_INVALIDARG,
                L"DFIR-Orc in offline mode and no computer name set, defaulting to this machine's name\r\n");
        }

        // Then, we set ORC_Offline as a "OnlyThis" keyword
        config.OnlyTheseKeywords.clear();
        config.OnlyTheseKeywords.insert(L"ORC_Offline");

        // Finally, we set the %OfflineLocation env var so the location is known to chikdren
        SetEnvironmentVariable(L"OfflineLocation", config.strOfflineLocation.value().c_str());
    }

    if (!config.OnlyTheseKeywords.empty())
    {
        for (const auto& exec : m_wolfexecs)
        {
            const auto& found = config.OnlyTheseKeywords.find(exec->GetKeyword());

            if (found != end(config.OnlyTheseKeywords))
            {
                exec->SetMandatory();
            }
            else
            {
                exec->SetOptional();
                for (const auto& command : exec->GetCommands())
                {
                    const auto& found = config.OnlyTheseKeywords.find(command->Keyword());

                    if (found != end(config.OnlyTheseKeywords))
                    {
                        exec->SetMandatory();
                        command->SetMandatory();
                    }
                    else
                    {
                        command->SetOptional();
                    }
                }
            }
        }
    }

    if (!config.EnableKeywords.empty())
    {
        for (const auto& exec : m_wolfexecs)
        {
            const auto& found = config.EnableKeywords.find(exec->GetKeyword());

            if (found != end(config.EnableKeywords))
            {
                exec->SetMandatory();
            }
            else
            {
                for (const auto& command : exec->GetCommands())
                {
                    const auto& found = config.EnableKeywords.find(command->Keyword());

                    if (found != end(config.EnableKeywords))
                    {
                        exec->SetMandatory();
                        command->SetMandatory();
                    }
                }
            }
        }
    }

    if (!config.DisableKeywords.empty())
    {
        for (const auto& exec : m_wolfexecs)
        {
            const auto& found = config.DisableKeywords.find(exec->GetKeyword());

            if (found != end(config.DisableKeywords))
            {
                exec->SetOptional();
                for (const auto& command : exec->GetCommands())
                {
                    command->SetOptional();
                }
            }
            else
            {
                for (const auto& command : exec->GetCommands())
                {
                    const auto& found = config.DisableKeywords.find(command->Keyword());

                    if (found != end(config.DisableKeywords))
                    {
                        command->SetOptional();
                    }
                }
            }
        }
    }

    return S_OK;
}
