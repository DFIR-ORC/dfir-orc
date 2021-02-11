//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "ConsoleConfiguration.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/cxx11/any_of.hpp>

#include "Log/UtilitiesLogger.h"
#include "Utils/Result.h"
#include "Configuration/ConfigFile_Common.h"
#include "Configuration/Option.h"
#include "Command/WolfLauncher/Console/Stream/StandardOutputRedirection.h"

using namespace Orc::Command;
using namespace Orc;

using namespace std::literals;

namespace {

constexpr auto kConsole = L"console"sv;
constexpr auto kOutput = L"output"sv;
constexpr auto kEncoding = L"encoding"sv;
constexpr auto kDisposition = L"disposition"sv;

enum ConsoleConfigItems
{
    CONFIGITEM_CONSOLE_OUTPUT = 0,
    CONFIGITEM_CONSOLE_ENUMCOUNT
};

void ParseConsoleOptions(std::vector<Option>& options, ConsoleConfiguration& configuration)
{
    for (auto& option : options)
    {
        if (!option.value || option.isParsed)
        {
            continue;
        }

        if (option.key == kOutput)
        {
            configuration.output.path = *option.value;
            option.isParsed = true;
            continue;
        }

        if (option.key == kDisposition)
        {
            configuration.output.disposition = ToFileDisposition(*option.value);
            option.isParsed = true;
            continue;
        }

        if (option.key == kEncoding)
        {
            configuration.output.encoding = ToEncoding(*option.value);
            option.isParsed = true;
            continue;
        }
    }
}

bool ParseConsoleArgument(std::wstring_view input, ConsoleConfiguration& configuration)
{
    std::vector<Option> options;
    if (!ParseSubArguments(input, kConsole, options))
    {
        return false;
    }

    if (options.size() == 0)
    {
        Log::Error(L"Missing console options in: '{}'", input);
        return false;
    }

    ParseConsoleOptions(options, configuration);

    if (std::all_of(std::cbegin(options), std::cend(options), [](auto& option) { return option.isParsed; }))
    {
        Log::Error(L"Failed to parse some console options in: '{}'", input);
        return false;
    }

    return true;
}

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

HRESULT ConsoleConfiguration::Register(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;

    hr = parent.AddChildNode(kConsole, dwIndex, ConfigItem::OPTION);
    if (FAILED(hr))
    {
        return hr;
    }

    auto& consoleNode = parent[dwIndex];
    hr = consoleNode.AddChild(kOutput, Orc::Config::Common::output, CONFIGITEM_CONSOLE_OUTPUT);
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

//  TODO: make a function to parse expected file attributes ?
void ConsoleConfiguration::Parse(const ConfigItem& item, ConsoleConfiguration& configuration)
{
    if (item[CONFIGITEM_CONSOLE_OUTPUT])
    {
        const auto& outputItem = item[CONFIGITEM_CONSOLE_OUTPUT];

        // For resolving pattern once context is initialized the 'path' attribute must be an std::wstring (and not
        // 'OutputSpec' or 'std::filesystem::path')
        OutputSpec output;
        output.Configure(outputItem);

        if (!outputItem.empty())
        {
            // Keep unresolved pattern string
            configuration.output.path = outputItem.c_str();
        }

        if (::HasValue(outputItem, CONFIG_OUTPUT_ENCODING))
        {
            configuration.output.encoding = ToEncoding(output.OutputEncoding);
        }

        if (::HasValue(outputItem, CONFIG_OUTPUT_DISPOSITION))
        {
            configuration.output.disposition = ToFileDisposition(output.disposition);
        }
    }
}

void ConsoleConfiguration::Parse(int argc, const wchar_t* argv[], ConsoleConfiguration& config)
{
    for (int i = 0; i < argc; i++)
    {
        std::wstring_view argument(argv[i] + 1);

        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (::ParseConsoleArgument(argument, config))
                {
                    return;
                }
        }
    }
}

void ConsoleConfiguration::Apply(StandardOutputRedirection& redirection, const ConsoleConfiguration& config)
{
    if (!config.output.path)
    {
        redirection.Disable();

        // NOTE: Could also remove logger's console sink file redirection but it could make more harm than good
        // like unexpected reference kept, multithreading...
        return;
    }

    if (config.output.encoding && config.output.encoding != Text::Encoding::Utf8)
    {
        Log::Warn(L"Unupported encoding for console output file: {}", config.output.encoding);
    }

    OutputSpec output = OutputSpec();
    HRESULT hr = output.Configure(*config.output.path);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to configure console output file with: '{}' [{}]", config.output.path, SystemError(hr));
        return;
    }

    std::error_code ec;
    const auto disposition = config.output.disposition.value_or(FileDisposition::Truncate);

    redirection.Open(output.Path, disposition, ec);
    if (ec)
    {
        Log::Error(L"Failed to redirect console output to '{}' [{}]", output.Path, ec);
        return;
    }
}

}  // namespace Command
}  // namespace Orc
