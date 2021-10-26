//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Text/Tree.h"
#include "Utils/TypeTraits.h"

#include "FSUtils.h"

namespace Orc {

namespace Command {
namespace Usage {

namespace detail {

template <typename T>
void PrintParameters(Orc::Text::Tree<T>& root)
{
}

template <typename T, typename ParameterList0, typename... ParameterLists>
void PrintParameters(Orc::Text::Tree<T>& root, ParameterList0&& parameterList0, ParameterLists&&... parameterLists)
{
    using CharT = Traits::underlying_char_type_t<T>;

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
template <typename T, typename U, typename V>
void PrintHeader(Orc::Text::Tree<T>& root, const U& commandLine, const V& description)
{
    root.Add(commandLine);
    root.AddEOL();

    if constexpr (Traits::is_iterable_v<Traits::remove_all_t<V>>)
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

template <typename T, typename U>
void PrintParameter(Orc::Text::Tree<T>& root, const U& parameterSwitch, const U& description)
{
    auto node = root.AddNode(parameterSwitch);
    if (!description.empty())
    {
        parameterNode.Add(description);
    }
}

// Print multiple lists of parameters with their description as child of 'rootNode'
template <typename T, typename ListsDescription, typename... ParameterLists>
auto PrintParameters(Orc::Text::Tree<T>& root, const ListsDescription& description, ParameterLists&&... parameters)
{
    auto node = root.AddNode(description);
    detail::PrintParameters(node, std::forward<ParameterLists>(parameters)...);
    return node;
}

constexpr std::array kUsageOutput = {Parameter {"/Out=<Directory|File.csv|Archive.7z>", "Output file or directory"}};

constexpr std::string_view kCategoryOutputParameters = "OUTPUT PARAMETERS";

template <typename T, typename... CustomParameterLists>
auto PrintOutputParameters(Orc::Text::Tree<T>& root, CustomParameterLists&&... customParameters)
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

template <typename T, typename... CustomParameterLists>
auto PrintLoggingParameters(Orc::Text::Tree<T>& root, CustomParameterLists&&... customParameters)
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

template <typename T, typename... CustomParameterLists>
auto PrintMiscellaneousParameters(Orc::Text::Tree<T>& root, CustomParameterLists&&... customParameters)
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

template <typename T, typename... CustomParameterLists>
auto PrintLimitsParameters(Orc::Text::Tree<T>& root, CustomParameterLists&&... customParameters)
{
    auto node = PrintParameters(root, "COLLECTION LIMITS PARAMETERS", kUsageLimits);
    detail::PrintParameters(node, std::forward<CustomParameterLists>(customParameters)...);
    return node;
}

constexpr auto kKnownLocations = Parameter {"/KnownLocations|/kl", "Scan a set of locations known to be of interest"};

template <typename T, typename... CustomParameterLists>
auto PrintLocationParameters(Orc::Text::Tree<T>& root, CustomParameterLists&&... customParameters)
{
    constexpr std::array kLocationsParameters = {
        Parameter {
            "Location", "Access path (ex: '\\\\.\\HarddiskVolume6', 'c:\\', '\\\\.\\PHYSICALDRIVE0', dump.dd, ...)"},
        Parameter {
            "/Altitude=<Exact|Highest|Lowest>",
            "Defines the strategy used to translate a given location into the optimal access path to the volume"},
        Parameter {
            "/Shadows[=newest,mid,oldest,{GUID},...]",
            "Add Volume Shadows Copies for selected volumes to parse"}};

    PrintParameters(root, "LOCATIONS PARAMETERS", kLocationsParameters);
}

template <typename T, typename ExampleList>
auto PrintExamples(Orc::Text::Tree<T>& root, ExampleList examples)
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

template <typename T>
auto PrintColumnSelectionParameter(
    Orc::Text::Tree<T>& root,
    const Orc::ColumnNameDef* pColumnNames,
    const Orc::ColumnNameDef* pAliases)
{
    {
        constexpr std::array kColumnsParameters = {
            Usage::Parameter {"/(+|-)<ColumnSelection,...>:<Filter>", "Custom list of columns to include or exclude"},
            Usage::Parameter {
                "/<DefaultColumnSelection>,...",
                "Default, All, DeepScan, Details, Hashes, Fuzzy, PeHashes, Dates, RefNums, Authenticode"}};

        Usage::PrintParameters(root, "COLUMNS PARAMETERS", kColumnsParameters);
    }

    {
        auto columnSyntaxNode = root.AddNode("COLUMNS SELECTION SYNTAX");
        columnSyntaxNode.AddEOL();

        auto filterSyntaxNode = columnSyntaxNode.AddNode("Filter specification: [/(+|-)<ColSelection>:<Filter>]");
        filterSyntaxNode.Add("'+' include columns");
        filterSyntaxNode.Add("'-' exclude columns ");
        filterSyntaxNode.Add("<ColSelection> is a comma separated list of columns or aliases");
        filterSyntaxNode.AddEOL();
    }

    {
        auto columnDescriptionNode = root.AddNode("COLUMNS DESCRIPTION");
        const ColumnNameDef* pCurCol = pColumnNames;
        while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
        {
            const auto columnName = fmt::format(L"{}:", pCurCol->szColumnName);
            columnDescriptionNode.Add("{:<33} {}", columnName, pCurCol->szColumnDesc);
            pCurCol++;
        }

        columnDescriptionNode.AddEOL();
    }

    {
        auto columnAliasesNode = root.AddNode("COLUMNS ALIASES");
        columnAliasesNode.AddEOL();
        columnAliasesNode.Add("Alias names can be used to specify more than one column.");
        columnAliasesNode.AddEOL();

        const ColumnNameDef* pCurAlias = pAliases;

        while (pCurAlias->dwIntention != Intentions::FILEINFO_NONE)
        {
            auto aliasNode = columnAliasesNode.AddNode("{}", pCurAlias->szColumnName);

            DWORD dwNumCol = 0;
            const ColumnNameDef* pCurCol = pColumnNames;
            while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
            {
                if (HasFlag(pCurAlias->dwIntention, pCurCol->dwIntention))
                {
                    PrintValue(aliasNode, pCurCol->szColumnName, pCurCol->szColumnDesc);
                    dwNumCol++;
                }
                pCurCol++;
            }

            aliasNode.AddEOL();
            pCurAlias++;
        }
    }

    root.AddEmptyLine();
}

}  // namespace Usage
}  // namespace Command
}  // namespace Orc
