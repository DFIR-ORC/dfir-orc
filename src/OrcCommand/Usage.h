//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <array>
#include <string>
#include <vector>

#include <boost/algorithm/string/split.hpp>

#include "Text/Tree.h"
#include "Utils/TypeTraits.h"

#include "FSUtils.h"

namespace Orc {

namespace Command {
namespace Usage {

namespace detail {

inline void PrintParameters(Orc::Text::Tree& root) {}

template <typename ParameterList0, typename... ParameterLists>
void PrintParameters(Orc::Text::Tree& root, ParameterList0&& parameterList0, ParameterLists&&... parameterLists)
{
    using CharT = Traits::underlying_char_type_t<Orc::Text::Tree>;

    for (const auto& [parameterSwitch, description] : parameterList0)
    {
        if (!description.empty())
        {
            auto node = root.AddNode(parameterSwitch);

            // TODO: look for a replacement with string_view
            std::vector<std::basic_string<CharT>> lines;
            boost::split(lines, description, [](CharT c) { return c == Traits::newline<CharT>::value; });
            for (const auto& line : lines)
            {
                node.Add(line);
            }
        }

        root.AddEOL();
    }

    PrintParameters(root, std::forward<ParameterLists>(parameterLists)...);
}

}  // namespace detail

using Parameter = std::pair<std::string_view, std::string_view>;
using Example = std::pair<std::string_view, std::string_view>;

// Format header of an ORC tool like GetThis. The 'Description' argument should be a char type or a list of char type.
template <typename T, typename U>
void PrintHeader(Orc::Text::Tree& root, const T& commandLine, const U& description)
{
    root.Add(commandLine);
    root.AddEOL();

    if constexpr (Traits::is_iterable_v<Traits::remove_all_t<U>>)
    {
        for (const auto paragraph : description)
        {
            root.Add(paragraph);
            root.AddEOL();
        }
    }
    else
    {
        root.Add(description);
        root.AddEOL();
    }
}

template <typename T>
void PrintParameter(Orc::Text::Tree& root, const T& parameterSwitch, const T& description)
{
    auto node = root.AddNode(parameterSwitch);
    if (!description.empty())
    {
        node.Add(description);
    }
}

// Print multiple lists of parameters with their description as child of 'rootNode'
template <typename ListsDescription, typename... ParameterLists>
auto PrintParameters(Orc::Text::Tree& root, const ListsDescription& description, ParameterLists&&... parameters)
{
    auto node = root.AddNode(description);
    detail::PrintParameters(node, std::forward<ParameterLists>(parameters)...);
    return node;
}

constexpr std::array kUsageOutput = {Parameter {"/Out=<Directory|File.csv|Archive.7z>", "Output file or directory"}};

constexpr std::string_view kCategoryOutputParameters = "OUTPUT PARAMETERS";

template <typename... CustomParameterLists>
auto PrintOutputParameters(Orc::Text::Tree& root, CustomParameterLists&&... customParameters)
{
    auto node = PrintParameters(root, kCategoryOutputParameters, kUsageOutput);
    detail::PrintParameters(node, std::forward<CustomParameterLists>(customParameters)...);
    return node;
}

constexpr std::array kUsageLogging = {
    Parameter("/Verbose", "Display logs on console at debug level"),
    Parameter(
        "/Log:{type},{option1,option2=value,...}",
        "Specify log output type ('file', 'console' or 'syslog') and related options (output, level, backtrace).\n"
        "Example: /Log:file,output=foo.log,level=info,backtrace=error"),
    Parameter("/LogFile=<filename>", "[DEPRECATED] Specify log file"),
    Parameter("/Trace", "Set trace log level"),
    Parameter("/Debug", "Set debug log level"),
    Parameter("/Info", "Set info log level"),
    Parameter("/Warn", "Set warn log level"),
    Parameter("/Error", "Set error log level"),
    Parameter("/Critical", "Set critical log level"),
    Parameter("/NoConsole", "Turns off console logging")};

template <typename... CustomParameterLists>
auto PrintLoggingParameters(Orc::Text::Tree& root, CustomParameterLists&&... customParameters)
{
    auto node = PrintParameters(root, "LOGGING PARAMETERS", kUsageLogging);
    detail::PrintParameters(node, std::forward<CustomParameterLists>(customParameters)...);
    return node;
}

constexpr std::array kUsageMiscellaneous = {
    Parameter("/Low", "Runs with lowered priority"),
    Parameter("/Config=<ConfigFile>", "XML configuration file overriding current values")};

constexpr auto kMiscParameterLocal = Usage::Parameter {
    "/Local=<File>",
    "Configuration file to override some option. Usefull for specific setup to change upload settings..."};

constexpr auto kMiscParameterComputer = Usage::Parameter {
    "/Computer=<ComputerName>",
    "Substitute computer name to the one returned by GetComputerName() api"};

constexpr auto kMiscParameterFullComputer = Usage::Parameter {
    "/FullComputer=<ComputerName>",
    "Substitute computer name to the one returned by 'GetComputerNameExW' api with "
    "'ComputerNamePhysicalDnsFullyQualified' argument"};

constexpr auto kMiscParameterResurrectRecords = Usage::Parameter {
    "/ResurrectRecords",
    "Include records marked as \"not in use\" in enumeration. (they will need the FILE tag)"};

constexpr auto kMiscParameterCompression =
    Usage::Parameter {"/Compression=<CompressionLevel>", "Set archive compression level"};

constexpr auto kMiscParameterPassword = Usage::Parameter {"/Password=<password>", "Set archive password if supported"};

constexpr auto kMiscParameterTempDir =
    Usage::Parameter {"/TempDir=<DirectoryPath>", "Use 'DirectoryPath' to store temporary files"};

template <typename... CustomParameterLists>
auto PrintMiscellaneousParameters(Orc::Text::Tree& root, CustomParameterLists&&... customParameters)
{
    auto node = PrintParameters(root, "MISCELLANEOUS PARAMETERS", kUsageMiscellaneous);
    detail::PrintParameters(node, std::forward<CustomParameterLists>(customParameters)...);
    return node;
}

constexpr std::array kUsageLimits = {
    Usage::Parameter {"/MaxSampleCount=<max_count>", "Do not collect more than 'max_count' samples"},
    Usage::Parameter {"/MaxPerSampleBytes=<max_byte>", "Do not collect sample bigger than <max_byte>"},
    Usage::Parameter {"/MaxTotalBytes=<max_byte>", "Do not collect more than 'max_byte'"},
    Usage::Parameter {"/NoLimits", "Do not set collection limit (output can get VERY big)"}};

template <typename... CustomParameterLists>
auto PrintLimitsParameters(Orc::Text::Tree& root, CustomParameterLists&&... customParameters)
{
    auto node = PrintParameters(root, "COLLECTION LIMITS PARAMETERS", kUsageLimits);
    detail::PrintParameters(node, std::forward<CustomParameterLists>(customParameters)...);
    return node;
}

constexpr auto kKnownLocations = Parameter {"/KnownLocations|/kl", "Scan a set of locations known to be of interest"};

template <typename... CustomParameterLists>
auto PrintLocationParameters(Orc::Text::Tree& root, CustomParameterLists&&... customParameters)
{
    constexpr std::array kLocationsParameters = {
        Parameter {
            "Location", "Access path (ex: '\\\\.\\HarddiskVolume6', 'c:\\', '\\\\.\\PHYSICALDRIVE0', dump.dd, ...)"},
        Parameter {
            "/Altitude=<Exact|Highest|Lowest>",
            "Defines the strategy used to translate a given location into the optimal access path to the volume"},
        Parameter {
            "/Shadows[=newest,mid,oldest,{GUID},...]", "Add Volume Shadows Copies for selected volumes to parse"},
        Parameter {
            "/Exclude=\"%SYSTEMDRIVE%\",D:",
            "Exclude volume from processing (does not exclude shadow copy volumes if enabled)"}};

    PrintParameters(root, "LOCATIONS PARAMETERS", kLocationsParameters);
}

template <typename ExampleList>
Text::Tree PrintExamples(Text::Tree& root, ExampleList examples)
{
    auto node = root.AddNode("EXAMPLES");
    for (const auto& [example, description] : examples)
    {
        auto commandNode = node.AddNode("- {}", description);
        commandNode.AddEmptyLine();
        commandNode.Add(example);
        commandNode.AddEmptyLine();
    }

    return node;
}

void PrintColumnSelectionParameter(
    Text::Tree& root,
    const Orc::ColumnNameDef* pColumnNames,
    const Orc::ColumnNameDef* pAliases);

}  // namespace Usage
}  // namespace Command
}  // namespace Orc
