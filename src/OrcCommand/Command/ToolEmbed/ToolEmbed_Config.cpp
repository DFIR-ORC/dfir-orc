//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <Psapi.h>

#include <string>
#include <boost/algorithm/string.hpp>

#include "OrcResource.h"
#include "ToolEmbed.h"
#include "ParameterCheck.h"
#include "EmbeddedResource.h"
#include "SystemDetails.h"

#include "ConfigFile_ToolEmbed.h"
#include "Configuration/ConfigFileReader.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::ToolEmbed;

namespace {

Result<std::wstring> GetResourceString(HINSTANCE hInstance, UINT uID)
{
    wchar_t* pString = nullptr;

    // Passing 0 as the third argument returns a pointer to the resource itself (read-only)
    int length = LoadStringW(hInstance, uID, reinterpret_cast<LPWSTR>(&pString), 0);
    DWORD lastError = GetLastError();
    if (lastError != ERROR_SUCCESS)
    {
        auto ec = std::error_code(lastError, std::system_category());
        Log::Debug(L"Failed LoadStringW (id: {}) [{}]", uID, ec);
        return ec;
    }

    if (length == 0 || pString == nullptr)
    {
        Log::Debug(L"Failed LoadStringW (id: {})", uID);
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    return std::wstring(pString, length);
}

bool IsCapsuleExecutable(const std::wstring& path)
{
    std::error_code ec;

    Guard::Module module =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed LoadLibraryExW (path: {}) [{}]", path, ec);
        return false;
    }

    auto value = GetResourceString(module.value(), IDS_BOOTSTRAP);
    if (!value)
    {
        Log::Debug(L"Failed to detect architecture from resources (path: {}) [{}]", path, value.error());
        return false;
    }

    return *value == L"capsule";
}

Result<void> GetTopDirectoryAndFilename(
    const std::filesystem::path& path,
    std::optional<std::wstring>& topDirectory,
    std::optional<std::wstring>& filename)
{
    std::error_code ec;

    bool isExists = std::filesystem::exists(path, ec);
    if (ec)
    {
        Log::Debug(L"Failed std::filesystem::exists (path: {}) [{}]", path, ec);
        return ec;
    }

    if (!isExists)
    {
        Log::Debug(L"Path does not exists: {}", path);
        return std::errc::no_such_file_or_directory;
    }

    bool isDirectory = std::filesystem::is_directory(path, ec);
    if (ec)
    {
        Log::Debug(L"Failed std::filesystem::is_directory (path: {}) [{}]", path, ec);
        return ec;
    }

    auto absolutePath = std::filesystem::absolute(path, ec);
    if (ec)
    {
        Log::Debug(L"Failed std::filesystem::absolute (path: {}) [{}]", path, ec);
        return ec;
    }

    if (isDirectory)
    {
        filename = absolutePath / L"embed.xml";
        topDirectory = absolutePath;
    }
    else
    {
        filename = absolutePath;
        topDirectory = absolutePath.parent_path();
    }

    return Orc::Success<void>();
}

}  // namespace

ConfigItem::InitFunction Main::GetXmlConfigBuilder()
{
    return Orc::Config::ToolEmbed::root;
}

HRESULT
Main::GetNameValuePairFromConfigItem(const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    if (wcscmp(item.strName.c_str(), L"pair"))
    {
        Log::Debug("item passed is not a name value pair");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_PAIRNAME])
    {
        Log::Debug("name attribute is missing in name value pair");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_PAIRVALUE])
    {
        Log::Debug("value attribute is missing in name value pair");
        return E_FAIL;
    }

    spec = EmbeddedResource::EmbedSpec::AddNameValuePair(item[TOOLEMBED_PAIRNAME], item[TOOLEMBED_PAIRVALUE]);
    return S_OK;
}

HRESULT Main::GetAddFileFromConfigItem(const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    if (wcscmp(item.strName.c_str(), L"file"))
    {
        Log::Debug("item passed is not a file addition request");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_FILENAME])
    {
        Log::Debug("name attribute is missing in file addition request");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_FILEPATH])
    {
        Log::Debug("path attribute is missing in file addition request");
        return E_FAIL;
    }

    wstring strInputFile;

    if (item)
    {
        HRESULT hr = E_FAIL;
        if (FAILED(hr = ExpandFilePath(((const std::wstring&)item[TOOLEMBED_FILEPATH]).c_str(), strInputFile, false)))
        {
            Log::Error(
                L"Error in specified file '{}' to add in config file [{}]",
                item[TOOLEMBED_FILEPATH].c_str(),
                SystemError(hr));
            return hr;
        }
    }

    spec = EmbeddedResource::EmbedSpec::AddFile((const std::wstring&)item[TOOLEMBED_FILENAME], std::move(strInputFile));
    return S_OK;
}

HRESULT Main::GetAddArchiveFromConfigItem(const ConfigItem& item, EmbeddedResource::EmbedSpec& spec)
{
    HRESULT hr = E_FAIL;

    if (wcscmp(item.strName.c_str(), L"archive"))
    {
        Log::Debug("item passed is not a archive addition request");
        return E_FAIL;
    }

    if (!item[TOOLEMBED_ARCHIVE_NAME])
    {
        Log::Debug("name attribute is missing in archive addition request");
        return E_FAIL;
    }

    wstring strArchiveName = (const std::wstring&)item[TOOLEMBED_ARCHIVE_NAME];

    wstring strArchiveFormat;
    if (item[TOOLEMBED_ARCHIVE_FORMAT])
    {
        strArchiveFormat = (const std::wstring&)item[TOOLEMBED_ARCHIVE_FORMAT];
    }

    wstring strArchiveCompression;
    if (item[TOOLEMBED_ARCHIVE_COMPRESSION])
    {
        strArchiveCompression = (const std::wstring&)item[TOOLEMBED_ARCHIVE_COMPRESSION];
    }

    if (!item[TOOLEMBED_FILE2ARCHIVE])
    {
        Log::Debug("files to archive items are missing in archive addition request");
        return E_FAIL;
    }

    std::vector<EmbeddedResource::EmbedSpec::ArchiveItem> items;

    hr = S_OK;
    std::for_each(
        begin(item[TOOLEMBED_FILE2ARCHIVE].NodeList),
        end(item[TOOLEMBED_FILE2ARCHIVE].NodeList),
        [&items, &hr](const ConfigItem& item2cab) {
            EmbeddedResource::EmbedSpec::ArchiveItem toCab;

            toCab.Name = item2cab[TOOLEMBED_FILE2ARCHIVE_NAME];

            wstring strInputFile;

            HRESULT subhr = E_FAIL;
            if (FAILED(subhr = ExpandFilePath(item2cab[TOOLEMBED_FILE2ARCHIVE_PATH].c_str(), strInputFile, false)))
            {
                Log::Error(
                    L"Error in specified file '{}' to add to archive [{}]",
                    item2cab[TOOLEMBED_FILE2ARCHIVE_PATH].c_str(),
                    SystemError(subhr));
                hr = subhr;
                return;
            }

            std::swap(toCab.Path, strInputFile);

            items.push_back(toCab);
        });

    if (FAILED(hr))
        return hr;

    spec = EmbeddedResource::EmbedSpec::AddArchive(
        std::move(strArchiveName), std::move(strArchiveFormat), std::move(strArchiveCompression), std::move(items));

    return S_OK;
}

HRESULT Main::GetConfigurationFromConfig(const ConfigItem& configitem)
{
    HRESULT hr = E_FAIL;

    ConfigFile reader;

    {
        if (FAILED(hr = reader.GetInputFile(configitem[TOOLEMBED_INPUT], config.strInputFile, false)))
            return hr;
    }

    {
        if (FAILED(
                hr = config.Output.Configure(
                    static_cast<OutputSpec::Kind>(OutputSpec::Kind::File | OutputSpec::Kind::Directory),
                    configitem[TOOLEMBED_OUTPUT])))
        {
            return hr;
        }
    }

    hr = S_OK;

    if (configitem[TOOLEMBED_PAIR])
    {
        std::for_each(
            begin(configitem[TOOLEMBED_PAIR].NodeList),
            end(configitem[TOOLEMBED_PAIR].NodeList),
            [this, &hr](const ConfigItem& item) {
                HRESULT local_hr = E_FAIL;
                EmbeddedResource::EmbedSpec spec = EmbeddedResource::EmbedSpec::AddVoid();

                if (SUCCEEDED(local_hr = GetNameValuePairFromConfigItem(item, spec)))
                {
                    // Postpone push_back for those in CheckConfiguration because it is deprecated when using capsule
                    if (spec.Name == L"RUN")
                    {
                        config.m_run = spec.Value;
                    }
                    else if (spec.Name == L"RUN32")
                    {
                        config.m_run32 = spec.Value;
                    }
                    else if (spec.Name == L"RUN64")
                    {
                        config.m_run64 = spec.Value;
                    }
                    else if (spec.Name == L"RUN_ARGS")
                    {
                        config.m_runArgs = spec.Value;
                    }
                    else if (spec.Name == L"RUN32_ARGS")
                    {
                        config.m_runArgs32 = spec.Value;
                    }
                    else if (spec.Name == L"RUN64_ARGS")
                    {
                        config.m_runArgs64 = spec.Value;
                    }
                    else
                    {
                        if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::Void)
                            config.ToEmbed.push_back(std::move(spec));
                    }
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;
    hr = S_OK;

    if (configitem[TOOLEMBED_FILE])
    {
        std::for_each(
            begin(configitem[TOOLEMBED_FILE].NodeList),
            end(configitem[TOOLEMBED_FILE].NodeList),
            [this, &hr](const ConfigItem& item) {
                HRESULT local_hr = E_FAIL;
                EmbeddedResource::EmbedSpec spec = EmbeddedResource::EmbedSpec::AddVoid();

                if (SUCCEEDED(local_hr = GetAddFileFromConfigItem(item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::Void)
                        config.ToEmbed.push_back(std::move(spec));
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;
    hr = S_OK;

    if (configitem[TOOLEMBED_ARCHIVE])
    {
        std::for_each(
            begin(configitem[TOOLEMBED_ARCHIVE].NodeList),
            end(configitem[TOOLEMBED_ARCHIVE].NodeList),
            [this, &hr](const ConfigItem& item) {
                HRESULT local_hr = E_FAIL;
                EmbeddedResource::EmbedSpec spec = EmbeddedResource::EmbedSpec::AddVoid();

                if (SUCCEEDED(local_hr = GetAddArchiveFromConfigItem(item, spec)))
                {
                    if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::Void)
                        config.ToEmbed.push_back(std::move(spec));
                }
                else
                    hr = local_hr;
            });
    }

    if (FAILED(hr))
        return hr;

    if (configitem[TOOLEMBED_LOGGING])
    {
        Log::Warn(L"The '<logging> configuration element is deprecated, please use '<log>' instead");
    }

    if (configitem[TOOLEMBED_LOG])
    {
        UtilitiesLoggerConfiguration::Parse(configitem[TOOLEMBED_LOG], m_utilitiesConfig.log);
    }

    if (configitem[TOOLEMBED_RUN])
    {
        config.m_run = configitem[TOOLEMBED_RUN];
        if (configitem[TOOLEMBED_RUN][TOOLEMBED_RUN_ARGS])
        {
            config.m_runArgs = configitem[TOOLEMBED_RUN][TOOLEMBED_RUN_ARGS];
        }
    }

    if (configitem[TOOLEMBED_RUN32])
    {
        config.m_run32 = configitem[TOOLEMBED_RUN32];
        if (configitem[TOOLEMBED_RUN32][TOOLEMBED_RUN_ARGS])
        {
            config.m_runArgs32 = configitem[TOOLEMBED_RUN32][TOOLEMBED_RUN_ARGS];
        }
    }

    if (configitem[TOOLEMBED_RUN64])
    {
        config.m_run64 = configitem[TOOLEMBED_RUN64];
        if (configitem[TOOLEMBED_RUN64][TOOLEMBED_RUN_ARGS])
        {
            config.m_runArgs64 = configitem[TOOLEMBED_RUN64][TOOLEMBED_RUN_ARGS];
        }
    }

    return S_OK;
}

HRESULT Main::GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[])
{
    UtilitiesLoggerConfiguration::Parse(argc, argv, m_utilitiesConfig.log);

    HRESULT hr = E_FAIL;

    std::wstring strParameter;

    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-': {
                std::optional<std::wstring> optionalParameter(strParameter);
                if (OptionalParameterOption(argv[i] + 1, L"Dump", optionalParameter))
                {
                    config.Todo = Main::Dump;
                    if (optionalParameter && !optionalParameter->empty())
                    {
                        config.strInputFileFromCli = *optionalParameter;
                    }
                }
                else if (OptionalParameterOption(argv[i] + 1, L"FromDump", optionalParameter))
                {
                    config.Todo = Main::FromDump;
                    if (optionalParameter && !optionalParameter->empty())
                    {
                        config.m_embedPath = *optionalParameter;
                    }
                }
                else if (OptionalParameterOption(argv[i] + 1, L"Embed", optionalParameter))
                {
                    config.Todo = Main::FromDump;
                    if (optionalParameter && !optionalParameter->empty())
                    {
                        config.m_embedPath = *optionalParameter;
                    }
                }
                else if (InputFileOption(argv[i] + 1, L"Input", config.strInputFileFromCli))
                    ;
                else if (OutputOption(
                             argv[i] + 1,
                             L"Out",
                             static_cast<OutputSpec::Kind>(OutputSpec::Kind::File | OutputSpec::Kind::Directory),
                             config.Output))
                    ;
                else if (OptionalParameterOption(argv[i] + 1, L"Run32", config.m_run32))
                {
                }
                else if (OptionalParameterOption(argv[i] + 1, L"Run64", config.m_run64))
                    ;
                else if (OptionalParameterOption(argv[i] + 1, L"Run", config.m_run))
                    ;
                else if (BooleanOption(argv[i] + 1, L"Force", config.m_force))
                    ;
                else if (ParameterOption(argv[i] + 1, L"AddFile", strParameter))
                {
                    static std::wregex r(L"([a-zA-Z0-9\\\\ \\-_\\.:%]*),([a-zA-Z0-9\\-_\\.]+)");
                    std::wsmatch s;

                    if (std::regex_match(strParameter, s, r))
                    {
                        WCHAR szFile[ORC_MAX_PATH];

                        if (FAILED(hr = ExpandFilePath(s[1].str().c_str(), szFile, ORC_MAX_PATH)))
                        {
                            Log::Error(L"Invalid file to embed specified: {}", strParameter);
                            return E_INVALIDARG;
                        }
                        if (s[2].matched)
                        {
                            config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddFile(s[2].str(), s[1].str()));
                        }
                    }
                    else
                    {
                        Log::Error(L"Option /AddFile should be like: /AddFile=File.cab,Name");
                        return E_INVALIDARG;
                    }
                }
                else if (ParameterOption(argv[i] + 1, L"Remove", strParameter))
                {
                    static std::wregex r(L"([a-zA-Z0-9\\-_\\.]*)");
                    std::wsmatch s;

                    if (std::regex_match(strParameter, s, r))
                    {
                        config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddBinaryDeletion(s[1].str()));
                    }
                    else
                    {
                        Log::Error("Option /Remove should be like: /Remove=101 or /Remove=ResourceCab");
                        return E_INVALIDARG;
                    }
                }
                else if (OptionalParameterOption(argv[i] + 1, L"Capsule", config.m_strCapsule))
                    ;
                else if (UsageOption(argv[i] + 1))
                    ;
                else if (ParameterOption(argv[i] + 1, L"Config", config.strConfigFile))
                    ;
                else if (IgnoreCommonOptions(argv[i] + 1))
                    ;
                else
                {
                    LPCWSTR pEquals = wcschr(argv[i] + 1, L'=');
                    if (pEquals)
                    {
                        static std::wregex r(L"([a-zA-Z0-9_]+)=(.*)");
                        std::wsmatch s;
                        std::wstring str(argv[i] + 1);

                        if (std::regex_match(str, s, r))
                        {
                            config.ToEmbed.push_back(
                                EmbeddedResource::EmbedSpec::AddNameValuePair(s[1].str(), s[2].str()));
                        }
                        else
                        {
                            Log::Error("Option should be like: /Name=Value");
                            return E_INVALIDARG;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    return S_OK;
}

HRESULT Main::CheckConfiguration()
{
    HRESULT hr = E_FAIL;
    std::error_code ec;

    if (!m_utilitiesConfig.log.level && !m_utilitiesConfig.log.console.level)
    {
        m_utilitiesConfig.log.console.level = Log::Level::Info;
    }

    UtilitiesLoggerConfiguration::Apply(m_logging, m_utilitiesConfig.log);

    if ((config.Output.IsFile() || config.Output.IsDirectory()) && !config.Output.Path.empty()
        && !std::filesystem::path(config.Output.Path).is_absolute())
    {
        auto output = std::filesystem::absolute(config.Output.Path, ec);
        if (ec)
        {
            Log::Debug(L"Failed std::filesystem::absolute (path: {}) [{}]", config.Output.Path, ec);
            ec.clear();
        }
        else
        {
            config.Output.Path = output;
        }
    }

    // Convert /AddFile path to absolute path
    for (auto& item : config.ToEmbed)
    {
        if (item.Value.find(L":#") != std::wstring::npos)
        {
            continue;
        }

        if (item.Type == EmbeddedResource::EmbedSpec::EmbedType::Archive)
        {
            for (auto& archiveItem : item.ArchiveItems)
            {
                auto absolutePath = std::filesystem::absolute(archiveItem.Path, ec);
                if (ec)
                {
                    Log::Debug(
                        L"Failed to convert to absolute path archive item (archive: {}, name: {}, value: {}) [{}]",
                        item.Name,
                        archiveItem.Name,
                        archiveItem.Path,
                        ec);
                    return E_FAIL;
                }

                archiveItem.Path = absolutePath;
            }
        }

        if (!std::filesystem::path(item.Value).is_absolute())
        {
            auto absolutePath = std::filesystem::absolute(item.Value, ec);
            if (ec)
            {
                Log::Debug(
                    L"Failed to convert to absolute path item (name: {}, value: {}) [{}]", item.Name, item.Value, ec);
                return E_FAIL;
            }

            item.Value = absolutePath;
        }
    }

    if (config.m_strCapsule)
    {
        auto handle = Text::FromHexToLittleEndian<HANDLE>(std::wstring_view(*config.m_strCapsule));
        if (handle)
        {
            m_hCapsule = handle.value();

            std::wstring capsule;
            capsule.resize(ORC_MAX_PATH);
            DWORD length = GetModuleFileNameExW(static_cast<HMODULE>(*m_hCapsule), NULL, capsule.data(), ORC_MAX_PATH);
            auto lastError = LastWin32Error();
            if (length == 0 && lastError.value() != ERROR_SUCCESS)
            {
                Log::Error("Failed to get module file name from capsule handle [{}]", lastError);
                return E_INVALIDARG;  // TODO: is this the best choice ?
            }

            capsule.resize(length);
            m_capsule = std::move(capsule);
        }
        else
        {
            m_capsule = *config.m_strCapsule;
        }
    }

    // Resolve embed directory path and load embed.xml if it was not specified with '/config'
    if (config.Todo == Main::Embed || config.Todo == Main::FromDump)
    {
        if (!config.strConfigFile.empty())
        {
            auto rv = GetTopDirectoryAndFilename(config.strConfigFile, config.m_embedDirectory, config.m_embedFile);
            if (!rv)
            {
                Log::Debug(L"Invalid embed path: {} [{}]", config.strConfigFile, rv.error());
                return E_INVALIDARG;
            }

            //
            // In previous versions, XML configuration elements were relative to the working directory,
            // which was impractical and unexpected. Users could not execute 'toolembed' from any
            // location and get the same result - it had to be executed with consideration for the
            // relative paths of configuration elements like 'input'.
            //
            // For example, if the XML configuration 'input' was set to 'tools/app.exe', 'toolembed'
            // would look for 'app.exe' relative to the current working directory. The new behavior
            // treats 'tools/app.exe' as relative to the configuration file's directory.
            //
            // For backward compatibility: if the 'input' path matches exactly one file and is
            // relative to the working directory, it will be accepted with a deprecation warning.
            //
            // Input paths specified via CLI have precedence over paths from the config file and
            // are always relative to the working directory.
            //
            if (config.strInputFileFromCli.empty() && !config.strInputFile.empty()
                && !std::filesystem::path(config.strInputFile).is_absolute())
            {
                std::optional<std::wstring> inputRelativeToConfig;
                std::optional<std::wstring> inputRelativeToWorkingDirectory;

                {
                    std::wstring inputFile = std::filesystem::path(*config.m_embedDirectory) / config.strInputFile;
                    bool found = std::filesystem::exists(inputFile, ec);
                    if (ec)
                    {
                        Log::Debug(L"Failed std::filesystem::exists (path: {}) [{}]", inputFile, ec);
                        ec.clear();
                    }
                    else if (found)
                    {
                        inputRelativeToConfig = std::filesystem::absolute(inputFile, ec);
                        if (ec)
                        {
                            Log::Debug(L"Failed std::filesystem::absolute (path: {}) [{}]", inputFile, ec);
                            return E_INVALIDARG;
                        }
                    }
                }

                {
                    bool found = std::filesystem::exists(config.strInputFile, ec);
                    if (ec)
                    {
                        Log::Debug(L"Failed std::filesystem::exists (path: {}) [{}]", config.strInputFile, ec);
                        ec.clear();
                    }

                    if (found)
                    {
                        inputRelativeToWorkingDirectory = std::filesystem::absolute(config.strInputFile, ec);
                        if (ec)
                        {
                            Log::Debug(L"Failed std::filesystem::absolute (path: {}) [{}]", config.strInputFile, ec);
                            return E_INVALIDARG;
                        }
                    }
                }

                if (inputRelativeToConfig && inputRelativeToWorkingDirectory
                    && inputRelativeToConfig != inputRelativeToWorkingDirectory)
                {
                    Log::Error(
                        L"Found multiple possible 'input' file as relative path resolution has changed (new path: {}, "
                        L"deprecated path: {}).\n"
                        L"Relative paths from configuration are now expected to be from the configuration file "
                        L"directory.\n"
                        L"Specifying input relative to working directory is deprecated.\n",
                        *inputRelativeToConfig,
                        *inputRelativeToWorkingDirectory);
                    return E_INVALIDARG;
                }

                if (inputRelativeToConfig)
                {
                    config.strInputFile = *inputRelativeToConfig;
                }
                else if (inputRelativeToWorkingDirectory)
                {
                    Log::Warn(
                        L"[DEPRECATED] Configuration specify an 'input' file relative to working directory."
                        L" Please update your configuration to be relative to the configuration file directory.");

                    config.strInputFile = *inputRelativeToWorkingDirectory;
                    config.m_embedDirectory = std::filesystem::current_path(ec);
                    if (ec)
                    {
                        Log::Debug("Failed std::filesystem::current_path [{}]", ec);
                        return E_INVALIDARG;
                    }
                }
            }
        }
        else if (config.ToEmbed.empty() || config.m_embedPath)  // ToEmbed is empty if no /AddFile has been specified
        {
            if (!config.m_embedPath)
            {
                config.m_embedPath = std::filesystem::current_path();
                if (ec)
                {
                    Log::Debug("Failed std::filesystem::current_path [{}]", ec);
                    return E_INVALIDARG;
                }
            }

            auto rv = GetTopDirectoryAndFilename(*config.m_embedPath, config.m_embedDirectory, config.m_embedFile);
            if (!rv)
            {
                Log::Debug(L"Invalid embed path: {} [{}]", *config.m_embedPath, rv.error());
                return E_INVALIDARG;
            }

            //
            // Load the configuration from embed.xml, this will update some variable from 'config' like
            // 'strInputFile'.
            //
            // Need to temporarily change current directory to the embed config path so GetConfigurationFromConfig
            // works. A refactor is needed to handle correctly the path of the configuration items that are always
            // relative (I guess to embed.xml directory).
            //
            WCHAR szPreviousCurDir[ORC_MAX_PATH];
            GetCurrentDirectoryW(ORC_MAX_PATH, szPreviousCurDir);

            auto scopeGuard = Orc::Guard::CreateScopeGuard([&]() { SetCurrentDirectoryW(szPreviousCurDir); });
            SetCurrentDirectoryW(config.m_embedDirectory->c_str());

            ConfigItem embed;
            hr = Orc::Config::ToolEmbed::root(embed);
            if (FAILED(hr))
            {
                Log::Error("Failed to create config item to embed [{}]", SystemError(hr));
                return hr;
            }

            ConfigFileReader reader;
            hr = reader.ReadConfig(config.m_embedFile->c_str(), embed);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to read embed config file '{}' [{}]", *config.m_embedFile, SystemError(hr));
                return hr;
            }

            // BEWARE: this could overwrite 'strInputFile' and some other configuration variables
            hr = GetConfigurationFromConfig(embed);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to obtain embed configuration '{}' [{}]", *config.m_embedFile, SystemError(hr));
                return hr;
            }
        }
    }

    if (m_capsule)
    {
        if (!config.strInputFile.empty())
        {
            Log::Warn(L"[DEPRECATED] Ignored input file (path: {})", config.strInputFile);
        }

        if (!config.strInputFileFromCli.empty() && config.strInputFileFromCli != config.strInputFile)
        {
            Log::Warn(L"[DEPRECATED] Ignored input file (path: {})", config.strInputFileFromCli);
        }

        config.strInputFile.clear();
        config.strInputFileFromCli.clear();
    }
    else
    {
        Log::Critical(L"[DEPRECATED] DFIR-ORC does not use Mothership anymore, call ToolEmbed from Capsule wrapper");
        return E_FAIL;
    }

    if (m_capsule)
    {
        // HACK: Clear all deprecated items that were inserted on the "read configuration" phase.
        // Need a refactor because it is really hard to make this somewhere else.
        std::vector<std::wstring> nameFilters;
        std::wregex pattern(L"^(.*:#)(?:.*\\|)?([^|]+)$");

        auto FilterAdd = [&pattern](const std::optional<std::wstring>& s, std::vector<std::wstring>& filter) {
            std::wsmatch match;
            if (s && !s->empty() && std::regex_match(*s, match, pattern))
            {
                filter.push_back(match[2].str());
            }
        };

        FilterAdd(config.m_run, nameFilters);
        FilterAdd(config.m_run32, nameFilters);
        FilterAdd(config.m_run64, nameFilters);

        for (auto& resource : config.ToEmbed)
        {
            resource.ArchiveItems.erase(
                std::remove_if(
                    std::begin(resource.ArchiveItems),
                    std::end(resource.ArchiveItems),
                    [&](EmbeddedResource::EmbedSpec::ArchiveItem& s) {
                        for (const auto& name : nameFilters)
                        {
                            if (boost::iequals(s.Name, name))
                            {
                                Log::Warn(L"[DEPRECATED] Ignored archive item '{}'", s.Name);
                                return true;
                            }
                        }

                        return false;
                    }),
                std::end(resource.ArchiveItems));
        }

        if (config.m_run32)
        {
            Log::Warn("[DEPRECATED] Ignored element 'Run32'");
            config.m_run32.reset();
        }

        if (config.m_runArgs32)
        {
            Log::Warn("[DEPRECATED] Ignored element 'Run32Args'");
            config.m_runArgs32.reset();
        }

        if (config.m_run64)
        {
            Log::Warn("[DEPRECATED] Ignored element 'Run64'");
            config.m_run64.reset();
        }

        if (config.m_runArgs64)
        {
            Log::Warn("[DEPRECATED] Ignored element 'Run64Args'");
            config.m_runArgs64.reset();
        }

        if (config.m_run)
        {
            Log::Warn("[DEPRECATED] Ignored element 'Run'");
            config.m_run.reset();
        }

        if (config.m_runArgs)
        {
            Log::Warn("[DEPRECATED] Ignored element 'RunArgs'");
            config.m_runArgs64.reset();
        }
    }
    else
    {
        if (config.m_run32)
        {
            if (EmbeddedResource::IsResourceBased(*config.m_run32))
            {
                config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX86(*config.m_run32));
                if (config.m_runArgs32)
                {
                    config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRun32Args(*config.m_runArgs32));
                }
            }
            else
            {
                Log::Error(L"Invalid resource pattern specified: {}", *config.m_run32);
                return E_INVALIDARG;
            }
        }

        if (config.m_run64)
        {
            if (EmbeddedResource::IsResourceBased(*config.m_run64))
            {
                config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunX64(*config.m_run64));
                if (config.m_runArgs64)
                {
                    config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRun64Args(*config.m_runArgs64));
                }
            }
            else
            {
                Log::Error(L"Invalid resource pattern specified: {}", *config.m_run64);
                return E_INVALIDARG;
            }
        }

        if (config.m_run)
        {
            if (EmbeddedResource::IsResourceBased(*config.m_run))
            {
                config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRun(*config.m_run));
                if (config.m_runArgs)
                {
                    config.ToEmbed.push_back(EmbeddedResource::EmbedSpec::AddRunArgs(*config.m_runArgs));
                }
            }
            else
            {
                Log::Error(L"Invalid resource pattern specified: {}", *config.m_run);
                return E_INVALIDARG;
            }
        }
    }

    DWORD dwMajor = 0, dwMinor = 0;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    if (dwMajor < 6 && dwMinor < 2)
    {
        Log::Error("ToolEmbed cannot be used on downlevel platform, please run ToolEmbed on Windows 7+ systems");
        return E_NOTIMPL;
    }

    switch (config.Todo)
    {
        case Main::FromDump:
        case Main::Embed:
            if (config.Output.Type != OutputSpec::Kind::File)
            {
                Log::Error("ToolEmbed can only embed into an output file");
                return E_INVALIDARG;
            }
            break;
        case Main::Dump:
            if (!config.strInputFile.empty())
            {
                // /dump= requires the use of absolute paths
                WCHAR szFullPath[ORC_MAX_PATH] = {0};
                if (!GetFullPathName(config.strInputFile.c_str(), ORC_MAX_PATH, szFullPath, NULL))
                {
                    Log::Error(L"Failed to compute full path name for: '{}'", config.strInputFile);
                    return E_INVALIDARG;
                }
                config.strInputFile.assign(szFullPath);
            }

            if (config.Output.Type != OutputSpec::Kind::Directory)
            {
                Log::Error("ToolEmbed can only dump a configuration into a directory");
                return E_INVALIDARG;
            }
            break;
        default:
            break;
    }

    return S_OK;
}
