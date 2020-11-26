//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl(ANSSI)
//

#include "stdafx.h"

#include <optional>

#include <spdlog/cfg/env.h>

#include "UtilitiesLogger.h"
#include "ParameterCheck.h"
#include "Utils/Result.h"

using namespace Orc;

namespace {

std::shared_ptr<Logger::ConsoleSink> CreateConsoleSink()
{
    auto console = std::make_shared<Logger::ConsoleSink>();
    console->set_level(spdlog::level::critical);
    return console;
}

std::shared_ptr<Logger::FileSink> CreateFileSink()
{
    auto file = std::make_shared<Logger::FileSink>();

    // Allow all logs to be print, they will be filtered by the upstream level set by spdlog::set_level
    file->set_level(spdlog::level::trace);
    return file;
}

std::pair<std::shared_ptr<spdlog::logger>, std::shared_ptr<spdlog::logger>>
CreateFacilities(std::shared_ptr<Logger::ConsoleSink> consoleSink, std::shared_ptr<Logger::FileSink> fileSink)
{
    std::vector<std::shared_ptr<spdlog::logger>> loggers;

    auto defaultLogger = std::make_shared<spdlog::logger>("default", spdlog::sinks_init_list {consoleSink, fileSink});

    auto fileLogger = std::make_shared<spdlog::logger>("file", fileSink);
    fileSink->set_level(spdlog::level::trace);  // delegate filtering to the sink

    return {defaultLogger, fileLogger};
}

}  // namespace

Orc::Command::UtilitiesLogger::UtilitiesLogger()
{
    m_fileSink = CreateFileSink();
    m_consoleSink = CreateConsoleSink();

    auto [defaultLogger, fileLogger] = CreateFacilities(m_consoleSink, m_fileSink);

    auto loggers = {
        std::make_pair(Logger::Facility::kDefault, defaultLogger),
        std::make_pair(Logger::Facility::kLogFile, fileLogger)};
    m_logger = std::make_shared<Logger>(loggers);

    Orc::Log::SetDefaultLogger(m_logger);
}

Orc::Command::UtilitiesLogger::~UtilitiesLogger()
{
    Orc::Log::SetDefaultLogger(nullptr);
}

// Portage of LogFileWriter::ConfigureLoggingOptions
// Parse directly argc/argv to allow initializing logs very early
void Orc::Command::UtilitiesLogger::Configure(int argc, const wchar_t* argv[]) const
{
    HRESULT hr = E_FAIL;
    bool verbose = false;
    std::optional<spdlog::level::level_enum> level;

    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (!_wcsnicmp(argv[i] + 1, L"Verbose", wcslen(L"Verbose")))
                {
                    verbose = true;
                }

                if (!_wcsnicmp(argv[i] + 1, L"Critical", wcslen(L"Critical")))
                {
                    level = spdlog::level::critical;
                    verbose = true;
                }
                if (!_wcsnicmp(argv[i] + 1, L"Error", wcslen(L"Error")))
                {
                    level = spdlog::level::err;
                    verbose = true;
                }
                else if (!_wcsnicmp(argv[i] + 1, L"Warn", wcslen(L"Warn")))
                {
                    level = spdlog::level::warn;
                    verbose = true;
                }
                else if (!_wcsnicmp(argv[i] + 1, L"Info", wcslen(L"Info")))
                {
                    level = spdlog::level::info;
                    verbose = true;
                }
                else if (!_wcsnicmp(argv[i] + 1, L"Debug", wcslen(L"Debug")))
                {
                    level = spdlog::level::debug;
                    verbose = true;
                }
                else if (!_wcsnicmp(argv[i] + 1, L"Trace", wcslen(L"Trace")))
                {
                    level = spdlog::level::trace;
                    verbose = true;
                }
                else if (!_wcsnicmp(argv[i] + 1, L"NoConsole", wcslen(L"NoConsole")))
                {
                    verbose = false;
                }
                else if (!_wcsnicmp(argv[i] + 1, L"LogFile", wcslen(L"LogFile")))
                {
                    LPCWSTR pEquals = wcschr(argv[i], L'=');
                    WCHAR szLogFile[MAX_PATH] = {};
                    if (!pEquals)
                    {
                        Log::Error(L"Option /LogFile should be like: /LogFile=c:\\temp\\logfile.log\r\n");
                        continue;
                    }
                    else
                    {
                        if (FAILED(hr = GetOutputFile(pEquals + 1, szLogFile, MAX_PATH)))
                        {
                            Log::Error(L"Invalid logging file specified: {} [{}]", pEquals + 1, SystemError(hr));
                            continue;
                        }

                        std::error_code ec;
                        m_fileSink->Open(szLogFile, ec);
                        if (ec)
                        {
                            Log::Error(L"Failed to initialize log file '{}': {}", szLogFile, ec);
                            continue;
                        }
                    }
                }
        }
    }

    // Load log levels from environment variable (ex: "SPDLOG_LEVEL=info,mylogger=trace")
    spdlog::cfg::load_env_levels();

    if (verbose)
    {
        if (!level)
        {
            level = spdlog::level::debug;
        }

        m_consoleSink->set_level(*level);
    }

    if (level)
    {
        m_logger->Get(Logger::Facility::kDefault)->set_level(*level);
        m_logger->Get(Logger::Facility::kLogFile)->set_level(*level);
    }
}
