//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include <string>

#include "SystemDetails.h"
#include "FileFind.h"
#include "ToolVersion.h"

#include "Output/Text/Print.h"
#include "Output/Text/Fmt/formatter.h"
#include "Output/Text/Print/Bool.h"
#include "Output/Text/Print/LocationSet.h"
#include "Output/Text/Print/OutputSpec.h"
#include "Output/Text/Print/SearchTerm.h"
#include "Utils/MakeArray.h"
#include "Usage.h"

using namespace Orc::Command::GetThis;
using namespace Orc;

template <>
struct fmt::formatter<Orc::Command::GetThis::ContentSpec, wchar_t> : public fmt::formatter<fmt::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(const Orc::Command::GetThis::ContentSpec& content, FormatContext& ctx)
    {
        switch (content.Type)
        {
            case ContentType::STRINGS:
                return fmt::format_to(
                    ctx.out(), L"{} (min: {}, max: {})", ToString(content.Type), content.MinChars, content.MaxChars);
            case ContentType::DATA:
            case ContentType::RAW:
            default:
                return fmt::format_to(ctx.out(), ToString(content.Type));
        }
    }
};

namespace {

template <typename T>
void PrintValues(
    Orc::Text::Tree<T> root,
    const std::wstring description,
    const Orc::Command::GetThis::ListOfSampleSpecs& samples)
{
    if (samples.size() == 0)
    {
        // Handle empty list as 'Orc::Text::PrintValues' would
        PrintValues(root, description, samples);
        return;
    }

    auto samplesNode = root.AddNode(description);

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const auto& sample = samples[i];

        std::wstring sampleName;
        if (sample.Name.empty())
        {
            sampleName = fmt::format(L"#{}:", i);
        }
        else
        {
            sampleName = fmt::format(L"#{} {}:", i, sample.Name);
        }

        auto sampleNode = samplesNode.AddNode(
            L"{} {} (Count: {}, Size: {}, Total: {})",
            sampleName,
            sample.Content,
            sample.PerSampleLimits.dwMaxSampleCount,
            sample.PerSampleLimits.dwlMaxBytesPerSample,
            sample.PerSampleLimits.dwlMaxBytesTotal);

        for (const auto& term : sample.Terms)
        {
            Print(sampleNode, term);
        }

        root.AddEmptyLine();
    }
}

}  // namespace

namespace Orc {
namespace Command {
namespace GetThis {

std::wstring ToString(ContentType contentType)
{
    switch (contentType)
    {
        case ContentType::DATA:
            return L"DATA";
        case ContentType::INVALID:
            return L"INVALID";
        case ContentType::RAW:
            return L"RAW";
        case ContentType::STRINGS:
            return L"STRINGS";
    }

    return L"<Unhandled Type>";
}

}  // namespace GetThis
}  // namespace Command
}  // namespace Orc

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe GetThis [/Sample=<SampleFile>[:<Stream>|#<EAName>]] [/Content=<Data|Strings|Raw>] "
        "[/Config=GetThisConfig.xml] [/Out=<Folder|File.csv|Archive.7z>] <Location>...<LocationN>",
        MakeArray<std::string_view>(
            R"raw(GetThis was originally developed to assist with malicious sample collection but quickly evolved into a general purpose file collection tool.)raw",
            R"raw(While enumerating the specified file systems, GetThis searches for specific file indicators to collect in Alternate Data Streams, Extended Attributes and "any" NTFS attribute. Various conditions and patterns can be defined to restrict the search to interesting matches.)raw",
            R"raw(GetThis bypass file system locks and permissions using its own MFT parser and therefore allows collection of in-use registry files, Pagefile, Hyberfil, event log files, files with restrictive ACLs, files opened with exclusive rights (i.e. non-shared), malware using file-level API hooking.)raw",
            R"raw(To prevent interference from anti-virus software, it is recommended to store the samples in a password-protected archive.)raw"));

    constexpr std::array kSpecificParameters = {
        Usage::Parameter {
            "/Content=<Data|Strings|Raw>",
            "Retrieved content: copy data (default), strings or raw bytes (ex: compressed bytes if NTFS option is "
            "enabled)"},
        Usage::Parameter {"/ReportAll", "Add information about rejected samples (due to limits) to CSV"},
        Usage::Parameter {"/NoSigCheck", "Check only sample signatures from autoruns output"},
        Usage::Parameter {"/Hash=<MD5|SHA1|SHA256>", "Comma-separated list of hashes to compute"},
        Usage::Parameter {"/FuzzyHash=<SSDeep|TLSH>", "Comma-separated list of 'FuzzyHash' hashes to compute"},
        Usage::Parameter {"/Yara=<Rules.yara>", "List of Yara sources"}};
    Usage::PrintParameters(usageNode, "PARAMETERS", kSpecificParameters);

    Usage::PrintLimitsParameters(usageNode);

    constexpr std::array kCustomOutputParameters = {
        Usage::Parameter {"/GetThisConfig=<FilePath>", "Output path to 'GetThis' generated configuration"},
        Usage::Parameter {"/SampleInfo=<FilePath.csv>", "Sample related information"},
        Usage::Parameter {"/TimeLine=<FilePath.csv>", "Timeline related information"}};
    Usage::PrintOutputParameters(usageNode, kCustomOutputParameters);

    constexpr std::array kCustomMiscParameters = {
        Usage::kMiscParameterCompression,
        Usage::kMiscParameterPassword,
        Usage::kMiscParameterTempDir,
        Usage::Parameter {"/FlushRegistry", "Flushes registry hives using RegFlushKey API"}};
    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);

    Usage::PrintLoggingParameters(usageNode);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode(L"Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Output", config.Output);
    PrintValue(node, L"ReportAll", config.bReportAll);
    PrintValue(node, L"Hash", config.CryptoHashAlgs);
    PrintValue(node, L"FuzzyHash", config.FuzzyHashAlgs);
    PrintValue(node, L"NoLimits", config.limits.bIgnoreLimits);
    PrintValue(node, L"MaxBytesPerSample", config.limits.dwlMaxBytesPerSample);
    PrintValue(node, L"MaxTotalBytes", config.limits.dwlMaxBytesTotal);
    PrintValue(node, L"MaxSampleCount", config.limits.dwMaxSampleCount);

    PrintValues(node, L"Parsed locations", config.Locations.GetParsedLocations());

    PrintValue(node, L"Default content", config.content);
    ::PrintValues(node, L"Specific samples:", config.listofSpecs);
    PrintValues(node, L"Excluded samples", config.listOfExclusions);

    if (config.limits.bIgnoreLimits)
    {
        root.Add(L"WARNING: No limits are imposed to the size and number of collected samples");
    }

    root.AddEmptyLine();
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
