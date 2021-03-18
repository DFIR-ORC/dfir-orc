//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "UtilitiesLoggerConfiguration.h"

#include <vector>
#include <optional>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/cxx11/any_of.hpp>
#include <spdlog/cfg/env.h>

#include "Log/UtilitiesLogger.h"
#include "Configuration/ConfigFile_Common.h"
#include "Configuration/Option.h"
#include "ParameterCheck.h"
#include "OutputSpec.h"

using namespace Orc::Command;
using namespace Orc;
using namespace std::literals;

namespace {

constexpr auto kLog = L"log"sv;
constexpr auto kLevel = L"level"sv;
constexpr auto kBacktrace = L"backtrace"sv;
constexpr auto kLogFile = L"logfile"sv;
constexpr auto kOutput = L"output"sv;
constexpr auto kEncoding = L"encoding"sv;
constexpr auto kDisposition = L"disposition"sv;
constexpr auto kVerbose = L"verbose"sv;
constexpr auto kNoConsole = L"noconsole"sv;
constexpr auto kConsole = L"console"sv;
constexpr auto kFile = L"file"sv;

constexpr auto kSyslog = L"syslog"sv;
constexpr auto kHost = L"host"sv;
constexpr auto kPort = L"port"sv;

constexpr auto kTrace = L"trace"sv;
constexpr auto kDebug = L"debug"sv;
constexpr auto kInfo = L"info"sv;
constexpr auto kWarn = L"warn"sv;
constexpr auto kError = L"error"sv;
constexpr auto kCritical = L"critical"sv;

enum LogCommonConfigItems
{
    CONFIGITEM_LOG_COMMON_LEVEL = 0,
    CONFIGITEM_LOG_COMMON_BACKTRACE,
    CONFIGITEM_LOG_COMMON_ENUMCOUNT
};

enum LogConfigItems
{
    CONFIGITEM_LOG_CONSOLE_NODE = CONFIGITEM_LOG_COMMON_ENUMCOUNT,
    CONFIGITEM_LOG_LOGFILE_NODE,
    CONFIGITEM_LOG_SYSLOG_NODE,
    CONFIGITEM_LOG_ENUMCOUNT
};

enum LogFileConfigItems
{
    CONFIGITEM_LOG_LOGFILE_OUTPUT = CONFIGITEM_LOG_COMMON_ENUMCOUNT
};

enum SyslogConfigItems
{
    CONFIGITEM_LOG_SYSLOG_HOST = CONFIGITEM_LOG_COMMON_ENUMCOUNT,
    CONFIGITEM_LOG_SYSLOG_PORT
};

bool HasValue(const ConfigItem& item, DWORD dwIndex)
{
    if (item.SubItems.size() <= dwIndex)
    {
        return false;
    }

    if (!item.SubItems[dwIndex])
    {
        return false;
    }

    return true;
}

HRESULT RegisterCommonItems(ConfigItem& item)
{
    HRESULT hr = item.AddAttribute(kLevel, CONFIGITEM_LOG_COMMON_LEVEL, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = item.AddAttribute(kBacktrace, CONFIGITEM_LOG_COMMON_BACKTRACE, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

HRESULT RegisterConsoleItems(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = parent.AddChildNode(kConsole, dwIndex, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    auto& consoleNode = parent[dwIndex];

    hr = RegisterCommonItems(consoleNode);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

HRESULT RegisterFileOptions(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = parent.AddChildNode(kFile, dwIndex, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    auto& fileNode = parent[dwIndex];

    hr = RegisterCommonItems(fileNode);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = fileNode.AddChild(kOutput, Orc::Config::Common::output, CONFIGITEM_LOG_LOGFILE_OUTPUT);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

HRESULT RegisterSyslogOptions(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = parent.AddChildNode(kSyslog, dwIndex, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    auto& syslogNode = parent[dwIndex];

    hr = RegisterCommonItems(syslogNode);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = syslogNode.AddChildNode(kHost, CONFIGITEM_LOG_SYSLOG_HOST, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = syslogNode.AddChildNode(kPort, CONFIGITEM_LOG_SYSLOG_PORT, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

std::optional<Log::Level> ParseLogLevel(const ConfigItem& item)
{
    if (!item[CONFIGITEM_LOG_COMMON_LEVEL])
    {
        return {};
    }

    const auto level = Log::ToLevel(item[CONFIGITEM_LOG_COMMON_LEVEL]);
    if (!level)
    {
        Log::Error(L"Failed to parse log level: {} [{}]", item[CONFIGITEM_LOG_COMMON_LEVEL], level.error());
        return {};
    }

    return *level;
}

std::optional<Log::Level> ParseBacktraceLevel(const ConfigItem& item)
{
    if (!item[CONFIGITEM_LOG_COMMON_BACKTRACE])
    {
        return {};
    }

    const auto level = Log::ToLevel(item[CONFIGITEM_LOG_COMMON_BACKTRACE]);
    if (!level)
    {
        Log::Error(
            L"Failed to parse backtrace trigger level: {} [{}]", item[CONFIGITEM_LOG_COMMON_BACKTRACE], level.error());
        return {};
    }

    return *level;
}

bool ParseCommonOptions(std::vector<Option>& options, UtilitiesLoggerConfiguration::Output& output)
{
    for (auto& option : options)
    {
        if (option.isParsed)
        {
            continue;
        }

        if (option.key == kLevel && option.value)
        {
            const auto level = Orc::Log::ToLevel(*option.value);
            if (!level)
            {
                Log::Error(L"Failed to parse log level: {} [{}]", *option.value, level.error());
                continue;
            }

            output.level = *level;
            option.isParsed = true;
            continue;
        }

        if (option.key == kBacktrace && option.value)
        {
            const auto level = Orc::Log::ToLevel(*option.value);
            if (!level)
            {
                Log::Error(L"Failed to parse backtrace trigger level: {} [{}]", *option.value, level.error());
                continue;
            }

            output.backtraceTrigger = *level;
            option.isParsed = true;
            continue;
        }
    }

    return true;
}

bool ParseConsoleOptions(std::vector<Option>& options, UtilitiesLoggerConfiguration::Output& output)
{
    return ParseCommonOptions(options, output);
}

bool ParseFileOptions(std::vector<Option>& options, UtilitiesLoggerConfiguration::FileOutput& output)
{
    for (auto& option : options)
    {
        if (!option.value || option.isParsed)
        {
            continue;
        }

        if (option.key == kOutput)
        {
            output.path = *option.value;
            option.isParsed = true;
            continue;
        }

        if (option.key == kDisposition)
        {
            output.disposition = ValueOr(ToFileDisposition(*option.value), FileDisposition::Unknown);
            option.isParsed = true;
            continue;
        }

        if (option.key == kEncoding)
        {
            output.encoding = ValueOr(ToEncoding(*option.value), Text::Encoding::Unknown);
            option.isParsed = true;
            continue;
        }
    }

    if (!output.path.has_value())
    {
        return false;
    }

    if (!ParseCommonOptions(options, output))
    {
        return false;
    }

    return true;
}

bool ParseSyslogOptions(std::vector<Option>& options, UtilitiesLoggerConfiguration::SyslogOutput& output)
{
    for (auto& option : options)
    {
        if (!option.value || option.isParsed)
        {
            continue;
        }

        if (option.key == kHost)
        {
            output.host = *option.value;
            option.isParsed = true;
            continue;
        }

        if (option.key == kPort)
        {
            output.port = kPort;
            option.isParsed = true;
            continue;
        }
    }

    if (!output.host.has_value())
    {
        return false;
    }

    if (!ParseCommonOptions(options, output))
    {
        return false;
    }

    return true;
}

bool ParseLogArgument(std::wstring_view input, UtilitiesLoggerConfiguration& configuration)
{
    std::vector<Option> options;
    if (!ParseSubArguments(input, kLog, options))
    {
        return false;
    }

    if (options.size() == 0)
    {
        return false;
    }

    // First sub-option should be log sink identifier: console, file, ...
    constexpr std::array sinks = {kConsole, kFile, kSyslog};
    auto& sink = options.front();
    if (!boost::algorithm::any_of_equal(sinks, sink.key))
    {
        return false;
    }

    if (sink.key == kConsole)
    {
        sink.isParsed = ParseConsoleOptions(options, configuration.console);
    }
    else if (sink.key == kFile)
    {
        sink.isParsed = ParseFileOptions(options, configuration.file);
    }
    else if (sink.key == kSyslog)
    {
        sink.isParsed = ParseSyslogOptions(options, configuration.syslog);
    }

    return std::all_of(std::cbegin(options), std::cend(options), [](auto& option) { return option.isParsed; });
}

void ApplyConsoleSinkLevel(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    std::optional<Log::Level> consoleLevel;

    if (config.console.level)
    {
        consoleLevel = config.console.level;
    }
    else if (config.level)
    {
        consoleLevel = config.level;
    }
    else if (config.verbose)
    {
        consoleLevel = Log::Level::Debug;
    }

    if (consoleLevel)
    {
        logger.consoleSink()->SetLevel(*consoleLevel);
    }
    else
    {
        logger.consoleSink()->SetLevel(Log::Level::Critical);
    }
}

void ApplyConsoleSinkBacktraceTrigger(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    if (config.console.backtraceTrigger)
    {
        logger.consoleSink()->SetBacktraceTrigger(*config.console.backtraceTrigger);
    }
    else if (config.backtraceTrigger)
    {
        logger.consoleSink()->SetBacktraceTrigger(*config.backtraceTrigger);
    }
    else
    {
        logger.consoleSink()->SetBacktraceTrigger(Log::Level::Off);
    }
}


void ApplySyslogSinkLevel(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    if (config.syslog.level)
    {
        logger.syslogSink()->SetLevel(*config.syslog.level);
    }
    else
    {
        logger.syslogSink()->SetLevel(Log::Level::Info);
    }
}

void ApplyConsoleSinkConfiguration(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    ApplyConsoleSinkLevel(logger, config);
    ApplyConsoleSinkBacktraceTrigger(logger, config);
}

void ApplyFileSinkLevel(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    std::optional<Log::Level> fileLevel;

    if (config.file.level)
    {
        fileLevel = config.file.level;
    }
    else if (config.level)
    {
        fileLevel = config.level;
    }

    if (fileLevel)
    {
        logger.fileSink()->SetLevel(*fileLevel);
    }
    else
    {
        logger.fileSink()->SetLevel(Log::Level::Info);
    }
}

void ApplyFileSinkBacktraceTrigger(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    if (config.file.backtraceTrigger)
    {
        logger.fileSink()->SetBacktraceTrigger(*config.file.backtraceTrigger);
    }
    else if (config.backtraceTrigger)
    {
        logger.fileSink()->SetBacktraceTrigger(*config.backtraceTrigger);
    }
    else
    {
        logger.fileSink()->SetBacktraceTrigger(Log::Level::Error);
    }
}

Orc::Result<void> ApplyFileSinkConfiguration(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    ApplyFileSinkLevel(logger, config);
    ApplyFileSinkBacktraceTrigger(logger, config);

    std::filesystem::path path;
    FileDisposition disposition = FileDisposition::CreateNew;
    Text::Encoding encoding = Text::Encoding::Utf8;

    if (config.file.path)
    {
        OutputSpec output = OutputSpec();
        HRESULT hr = output.Configure(*config.file.path);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to configure log file with: '{}' [{}]", config.file.path, SystemError(hr));
            return SystemError(hr);
        }

        path = output.Path;
        disposition = config.file.disposition.value_or(FileDisposition::CreateNew);
        encoding = config.file.encoding.value_or(Text::Encoding::Utf8);
    }
    else if (config.logFile)
    {
        OutputSpec output = OutputSpec();
        HRESULT hr = output.Configure(*config.logFile);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to configure log file with: '{}' [{}]", config.file.path, SystemError(hr));
            return SystemError(hr);
        }

        path = output.Path;
    }
    else
    {
        Log::Debug(L"Failed to configure file sink: missing path");
        return std::errc::invalid_argument;
    }

    if (encoding != Text::Encoding::Utf8)
    {
        Log::Warn(L"Log file only support utf-8 encoding, utf-16 option will be overriden with utf-8");
        encoding = Text::Encoding::Utf8;
    }

    std::error_code ec;
    logger.fileSink()->Open(path, disposition, ec);
    return ec;
}

Orc::Result<void>
ApplySyslogSinkConfiguration(UtilitiesLogger& utilitiesLogger, const UtilitiesLoggerConfiguration& config)
{
    ApplySyslogSinkLevel(utilitiesLogger, config);

    if (config.syslog.backtraceTrigger)
    {
        utilitiesLogger.syslogSink()->SetBacktraceTrigger(*config.syslog.backtraceTrigger);
    }
    else
    {
        utilitiesLogger.syslogSink()->SetBacktraceTrigger(Log::Level::Off);
    }

    if (!config.syslog.host)
    {
        Log::Debug(L"Failed to configure syslog sink: missing host");
        return std::errc::invalid_argument;
    }

    std::error_code ec;
    const auto host = Utf16ToUtf8(*config.syslog.host, ec);
    if (ec)
    {
        Log::Error(L"Failed to configure syslog sink host [{}]", ec);
        return ec;
    }

    std::string port = "514";
    if (config.syslog.port)
    {
        port = Utf16ToUtf8(*config.syslog.port, ec);
        if (ec)
        {
            Log::Error(L"Failed to configure syslog sink port [{}]", ec);
            return ec;
        }
    }

    utilitiesLogger.syslogSink()->AddEndpoint(host, port, ec);
    if (ec)
    {
        return ec;
    }

    return ec;
}

void OutputConfigurationToOptions(const UtilitiesLoggerConfiguration::Output& output, std::vector<Option>& options)
{
    if (output.level)
    {
        options.emplace_back(kLevel, ToString(*output.level));
    }

    if (output.backtraceTrigger)
    {
        options.emplace_back(kBacktrace, ToString(*output.backtraceTrigger));
    }
}

void GenericConfigurationToOptions(const UtilitiesLoggerConfiguration& config, std::vector<Option>& options)
{
    if (config.logFile)
    {
        options.emplace_back(kLogFile, config.logFile.value().wstring());
    }

    if (config.level)
    {
        options.emplace_back(kLevel, ToString(*config.level));
    }

    if (config.backtraceTrigger)
    {
        options.emplace_back(kBacktrace, ToString(*config.backtraceTrigger));
    }

    if (config.verbose)
    {
        options.emplace_back(kVerbose, std::optional<std::wstring_view>());
    }
}

std::optional<std::wstring> GenericConfigurationToArguments(const UtilitiesLoggerConfiguration& config)
{
    std::vector<Option> options;
    GenericConfigurationToOptions(config, options);

    if (options.empty())
    {
        return {};
    }

    return Join(options, L"/", L"", L" ");
}

std::optional<std::wstring> ConsoleConfigurationToArgument(const UtilitiesLoggerConfiguration& config)
{
    std::vector<Option> options;
    OutputConfigurationToOptions(config.console, options);
    if (options.empty())
    {
        return {};
    }

    return fmt::format(L"/{}:{}{}", kLog, kConsole, Join(options, L",", L"", L""));
}

void FileConfigurationToOptions(const UtilitiesLoggerConfiguration::FileOutput& output, std::vector<Option>& options)
{
    OutputConfigurationToOptions(output, options);

    if (output.path)
    {
        options.emplace_back(kOutput, *output.path);
    }

    if (output.encoding)
    {
        options.emplace_back(kEncoding, ToString(*output.encoding));
    }

    if (output.disposition)
    {
        options.emplace_back(kDisposition, ToString(*output.disposition));
    }
}

std::optional<std::wstring> FileConfigurationToArgument(const UtilitiesLoggerConfiguration& config)
{
    std::vector<Option> options;
    FileConfigurationToOptions(config.file, options);
    if (options.empty())
    {
        return {};
    }

    return fmt::format(L"/{}:{}{}", kLog, kFile, Join(options, L",", L"", L""));
}

bool StartsWith(std::wstring_view string, std::wstring_view substring)
{
    if (string.size() < substring.size())
    {
        return false;
    }

    return boost::iequals(std::wstring_view(string.data(), substring.size()), substring);
}

}  // namespace

namespace Orc {
namespace Command {

// Portage of LogFileWriter::ConfigureLoggingOptions
// Parse directly argc/argv to allow initializing logs very early
void UtilitiesLoggerConfiguration::Parse(int argc, const wchar_t* argv[], UtilitiesLoggerConfiguration& config)
{
    for (int i = 0; i < argc; i++)
    {
        std::wstring_view arg(argv[i] + 1);

        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (::ParseLogArgument(arg, config))
                    ;
                else if (::StartsWith(arg, kVerbose))
                {
                    config.verbose = true;
                }
                else if (::StartsWith(arg, kCritical))
                {
                    config.level = Log::Level::Critical;
                }
                else if (::StartsWith(arg, kError))
                {
                    config.level = Log::Level::Error;
                }
                else if (::StartsWith(arg, kWarn))
                {
                    config.level = Log::Level::Warning;
                }
                else if (::StartsWith(arg, kInfo))
                {
                    config.level = Log::Level::Info;
                }
                else if (::StartsWith(arg, kDebug))
                {
                    config.level = Log::Level::Debug;
                }
                else if (::StartsWith(arg, kTrace))
                {
                    config.level = Log::Level::Trace;
                }
                else if (::StartsWith(arg, kNoConsole))
                {
                    config.verbose = false;
                }
                else if (::StartsWith(arg, kLogFile))
                {
                    // Legacy 'logfile'
                    LPCWSTR pEquals = wcschr(argv[i], L'=');
                    if (!pEquals)
                    {
                        Log::Error(L"Option /LogFile should be like: /LogFile=c:\\temp\\logfile.log\r\n");
                        continue;
                    }
                    else
                    {
                        HRESULT hr;
                        WCHAR szLogFile[MAX_PATH] = {};
                        if (FAILED(hr = GetOutputFile(pEquals + 1, szLogFile, MAX_PATH)))
                        {
                            Log::Error(L"Invalid logging file specified: {} [{}]", pEquals + 1, SystemError(hr));
                            continue;
                        }

                        config.logFile = szLogFile;
                    }
                }
        }
    }
}

HRESULT UtilitiesLoggerConfiguration::Register(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;

    hr = parent.AddChildNode(kLog, dwIndex, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    auto& logNode = parent[dwIndex];

    hr = ::RegisterCommonItems(logNode);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = ::RegisterConsoleItems(logNode, CONFIGITEM_LOG_CONSOLE_NODE);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = ::RegisterFileOptions(logNode, CONFIGITEM_LOG_LOGFILE_NODE);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = ::RegisterSyslogOptions(logNode, CONFIGITEM_LOG_SYSLOG_NODE);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

void UtilitiesLoggerConfiguration::Parse(const ConfigItem& item, UtilitiesLoggerConfiguration& configuration)
{
    configuration.level = ::ParseLogLevel(item);
    configuration.backtraceTrigger = ::ParseBacktraceLevel(item);

    if (item[CONFIGITEM_LOG_CONSOLE_NODE])
    {
        configuration.console.level = ::ParseLogLevel(item[CONFIGITEM_LOG_CONSOLE_NODE]);
        configuration.console.backtraceTrigger = ::ParseBacktraceLevel(item[CONFIGITEM_LOG_CONSOLE_NODE]);
    }

    if (item[CONFIGITEM_LOG_LOGFILE_NODE])
    {
        configuration.file.level = ::ParseLogLevel(item[CONFIGITEM_LOG_LOGFILE_NODE]);
        configuration.file.backtraceTrigger = ::ParseBacktraceLevel(item[CONFIGITEM_LOG_LOGFILE_NODE]);

        if (item[CONFIGITEM_LOG_LOGFILE_OUTPUT])
        {
            const auto& outputItem = item[CONFIGITEM_LOG_LOGFILE_NODE][CONFIGITEM_LOG_LOGFILE_OUTPUT];

            // For resolving pattern once context is initialized the 'path' attribute must be an std::wstring (and not
            // 'OutputSpec' or 'std::filesystem::path')
            OutputSpec output;
            output.Configure(outputItem);

            if (!outputItem.empty())
            {
                // Keep unresolved pattern string
                configuration.file.path = outputItem.c_str();
            }

            if (::HasValue(outputItem, CONFIG_OUTPUT_ENCODING))
            {
                configuration.file.encoding = ToEncoding(output.OutputEncoding);
            }

            if (::HasValue(outputItem, CONFIG_OUTPUT_DISPOSITION))
            {
                configuration.file.disposition = ToFileDisposition(output.disposition);
            }
        }
    }

    if (item[CONFIGITEM_LOG_SYSLOG_NODE])
    {
        configuration.syslog.level = ::ParseLogLevel(item[CONFIGITEM_LOG_SYSLOG_NODE]);
        configuration.syslog.backtraceTrigger = ::ParseBacktraceLevel(item[CONFIGITEM_LOG_SYSLOG_NODE]);

        const auto& host = item[CONFIGITEM_LOG_SYSLOG_NODE][CONFIGITEM_LOG_SYSLOG_HOST];
        if (!host.empty())
        {
            configuration.syslog.host = host;
        }

        const auto& port = item[CONFIGITEM_LOG_SYSLOG_NODE][CONFIGITEM_LOG_SYSLOG_PORT];
        if (!port.empty())
        {
            configuration.syslog.port = port;
        }
    }
}

void UtilitiesLoggerConfiguration::ApplyLogLevel(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    ApplyConsoleSinkLevel(logger, config);
    ApplyFileSinkLevel(logger, config);
    ApplySyslogSinkLevel(logger, config);

    // Load log levels from environment variable (ex: "SPDLOG_LEVEL=info,mylogger=trace")
    // BEWARE: 'config' is not updated with log levels defined by environment variables
    spdlog::cfg::load_env_levels();
}

void UtilitiesLoggerConfiguration::ApplyLogLevel(UtilitiesLogger& logger, int argc, const wchar_t* argv[])
{
    UtilitiesLoggerConfiguration config;
    UtilitiesLoggerConfiguration::Parse(argc, argv, config);
    ApplyLogLevel(logger, config);
}


void UtilitiesLoggerConfiguration::ApplyBacktraceTrigger(
    UtilitiesLogger& logger,
    const UtilitiesLoggerConfiguration& config)
{
    ApplyConsoleSinkBacktraceTrigger(logger, config);
    ApplyFileSinkBacktraceTrigger(logger, config);
}

void UtilitiesLoggerConfiguration::ApplyBacktraceTrigger(UtilitiesLogger& logger, int argc, const wchar_t* argv[])
{
    UtilitiesLoggerConfiguration config;
    UtilitiesLoggerConfiguration::Parse(argc, argv, config);
    ApplyBacktraceTrigger(logger, config);
}

void UtilitiesLoggerConfiguration::Apply(UtilitiesLogger& logger, const UtilitiesLoggerConfiguration& config)
{
    ::ApplyConsoleSinkConfiguration(logger, config);
    ::ApplyFileSinkConfiguration(logger, config);
    ::ApplySyslogSinkConfiguration(logger, config);
}

std::optional<std::wstring>
UtilitiesLoggerConfiguration::ToCommandLineArguments(const UtilitiesLoggerConfiguration& config)
{
    std::vector<std::optional<std::wstring>> items = {
        ::GenericConfigurationToArguments(config),
        ::ConsoleConfigurationToArgument(config),
        ::FileConfigurationToArgument(config)};

    std::vector<std::wstring> arguments;
    for (auto& item : items)
    {
        if (item.has_value())
        {
            arguments.push_back(std::move(*item));
        }
    }

    if (arguments.empty())
    {
        return {};
    }

    return boost::join(arguments, L" ");
}

}  // namespace Command
}  // namespace Orc
