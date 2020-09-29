//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#pragma once

#include "OrcCommand.h"

#include <boost/logic/tribool.hpp>

#include "UtilitiesMain.h"

#include "ConfigFileReader.h"
#include "ConfigFileReader.h"
#include "MFTWalker.h"
#include "Location.h"
#include "ArchiveCreate.h"
#include "FileFind.h"
#include "TableOutputWriter.h"

#include "ByteStream.h"
#include "OrcLimits.h"

#include "CryptoHashStream.h"
#include "FuzzyHashStream.h"

#include "Utils/TypeTraits.h"

#include <filesystem>
#include <vector>
#include <set>
#include <string>

#pragma managed(push, off)

constexpr auto GETTHIS_DEFAULT_MAXTOTALBYTES = (100 * 1024 * 1024);  // 50MB;
constexpr auto GETTHIS_DEFAULT_MAXPERSAMPLEBYTES = (15 * 1024 * 1024);  // 15MB
constexpr auto GETTHIS_DEFAULT_MAXSAMPLECOUNT = 500;

namespace Orc {

class YaraScanner;

namespace Command::GetThis {

enum LimitStatus
{

    SampleWithinLimits = 0,

    GlobalSampleCountLimitReached = 1,
    GlobalMaxBytesPerSample = 1 << 1,
    GlobalMaxBytesTotal = 1 << 2,

    LocalSampleCountLimitReached = 1 << 3,
    LocalMaxBytesPerSample = 1 << 4,
    LocalMaxBytesTotal = 1 << 5,

    FailedToComputeLimits = 1 << 7,

    NoLimits = 1 << 8
};

enum class ContentType
{
    INVALID = 0,
    DATA,
    STRINGS,
    RAW
};

std::wstring ToString(ContentType contentType);

class ContentSpec
{
public:
    ContentType Type = ContentType::INVALID;

    size_t MinChars = 0L;
    size_t MaxChars = 0L;
};

class SampleSpec
{
public:
    SampleSpec() {};

    SampleSpec(SampleSpec&& other)
    {
        std::swap(Name, other.Name);
        std::swap(PerSampleLimits, other.PerSampleLimits);
        std::swap(Content, other.Content);
        std::swap(Terms, other.Terms);
    }

    SampleSpec(const SampleSpec&) = default;

    std::wstring Name;
    Limits PerSampleLimits;
    ContentSpec Content;

    std::vector<std::shared_ptr<FileFind::SearchTerm>> Terms;
};

using ListOfSampleSpecs = std::vector<SampleSpec>;

class ORCUTILS_API Main : public UtilitiesMain
{
public:
    class Configuration : public UtilitiesMain::Configuration
    {
        friend class Main;

    public:
        Configuration()
            : Locations()
        {
            bAddShadows = boost::indeterminate;
        }
        bool bFlushRegistry = false;
        bool bReportAll = false;
        boost::logic::tribool bAddShadows;

        OutputSpec Output;

        ListOfSampleSpecs listofSpecs;
        std::vector<std::shared_ptr<FileFind::SearchTerm>> listOfExclusions;
        LocationSet Locations;

        Limits limits;
        ContentSpec content;

        std::wstring YaraSource;
        std::unique_ptr<YaraConfig> Yara;

        CryptoHashStream::Algorithm CryptoHashAlgs =
            CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1;
        FuzzyHashStream::Algorithm FuzzyHashAlgs = FuzzyHashStream::Algorithm::Undefined;

        ContentSpec GetContentSpecFromString(const std::wstring& str);

    private:
        static std::wregex g_ContentRegEx;
    };

private:
    Configuration config;

    class SampleRef
    {
    public:
        std::wstring SampleName;

        CBinaryBuffer MD5;
        CBinaryBuffer SHA1;
        CBinaryBuffer SHA256;

        CBinaryBuffer SSDeep;
        CBinaryBuffer TLSH;

        ULONGLONG SampleSize = 0LL;
        FILETIME CollectionDate;
        bool OffLimits = false;

        LONGLONG VolumeSerial;  //   |
        MFT_SEGMENT_REFERENCE FRN;  //   |--> Uniquely identifies a data stream to collect
        USHORT InstanceID;  //   |
        size_t AttributeIndex = 0;  // AttributeIndex because each attribute from the match is a sample
        ContentSpec Content;
        std::shared_ptr<CryptoHashStream> HashStream;
        std::shared_ptr<FuzzyHashStream> FuzzyHashStream;
        std::shared_ptr<ByteStream> CopyStream;
        GUID SnapshotID;

        std::vector<std::shared_ptr<FileFind::Match>> Matches;

        SampleRef()
        {
            CollectionDate.dwHighDateTime = 0L;
            CollectionDate.dwLowDateTime = 0L;
        }

        SampleRef(const SampleRef&) = default;

        SampleRef(SampleRef&& Other)
        {
            std::swap(SampleName, Other.SampleName);
            std::swap(MD5, Other.MD5);
            std::swap(SHA1, Other.SHA1);
            std::swap(SampleSize, Other.SampleSize);
            CollectionDate = Other.CollectionDate;
            OffLimits = Other.OffLimits;
            std::swap(Matches, Other.Matches);
            AttributeIndex = Other.AttributeIndex;
            InstanceID = Other.InstanceID;
            std::swap(HashStream, Other.HashStream);
            std::swap(CopyStream, Other.CopyStream);
            std::swap(Content, Other.Content);
            std::swap(SnapshotID, Other.SnapshotID);
        }

        bool operator<(const SampleRef& rigth) const
        {
            if (FRN.SegmentNumberLowPart != rigth.FRN.SegmentNumberLowPart)
                return FRN.SegmentNumberLowPart < rigth.FRN.SegmentNumberLowPart;

            if (AttributeIndex != rigth.AttributeIndex)
            {
                return false;
            }

            if (!Matches.empty() && !rigth.Matches.empty())
            {
                if (VolumeSerial != rigth.VolumeSerial)
                    return VolumeSerial < rigth.VolumeSerial;

                auto cmpresult = memcmp(&SnapshotID, &rigth.SnapshotID, sizeof(GUID));
                if (cmpresult != 0)
                    return cmpresult < 0;

                return InstanceID < rigth.InstanceID;
            }
            return false;
        };
    };

    struct SampleRefHasher
    {
        size_t operator()(const SampleRef& ref) const { return ref.FRN.SegmentNumberLowPart; }
    };

    struct SampleRefComparator
    {
        bool operator()(const SampleRef& lhs, const SampleRef& rhs) const
        {
            if (lhs.FRN.SegmentNumberLowPart != rhs.FRN.SegmentNumberLowPart)
            {
                return false;
            }

            if (lhs.AttributeIndex != rhs.AttributeIndex)
            {
                return false;
            }

            if (lhs.Matches.empty() || rhs.Matches.empty())
            {
                return false;
            }

            if (lhs.VolumeSerial != rhs.VolumeSerial)
            {
                return false;
            }

            if (memcmp(&lhs.SnapshotID, &rhs.SnapshotID, sizeof(GUID)) != 0)
            {
                return false;
            }

            return true;
        }
    };

    using SampleSet = std::unordered_set<SampleRef, SampleRefHasher, SampleRefComparator>;
    SampleSet Samples;

    FileFind FileFinder;
    FILETIME CollectionDate;
    std::wstring ComputerName;
    Limits GlobalLimits;
    std::unordered_set<std::wstring> SampleNames;

    static HRESULT CreateSampleFileName(
        const ContentSpec& content,
        const PFILE_NAME pFileName,
        const std::wstring& DataName,
        DWORD idx,
        std::wstring& SampleFileName);

    HRESULT ConfigureSampleStreams(SampleRef& sampleRef);

    static LimitStatus SampleLimitStatus(const Limits& GlobalLimits, const Limits& LocalLimits, DWORDLONG DataSize);

    HRESULT
    AddSamplesForMatch(LimitStatus status, const SampleSpec& aSpec, const std::shared_ptr<FileFind::Match>& aMatch);

    HRESULT AddSampleRefToCSV(ITableOutput& output, const std::wstring& strComputerName, const SampleRef& sampleRef);

    std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
    CreateOutputDirLogFileAndCSV(const std::wstring& pOutputDir);

    std::pair<HRESULT, std::shared_ptr<TableOutput::IStreamWriter>> CreateArchiveLogFileAndCSV(
        const std::filesystem::path& archivePath,
        const std::shared_ptr<ArchiveCreate>& compressor) const;

    HRESULT CollectMatchingSamples(
        const std::shared_ptr<ArchiveCreate>& compressor,
        ITableOutput& output,
        SampleSet& MatchingSamples);
    HRESULT CollectMatchingSamples(const std::wstring& strOutputDir, ITableOutput& output, SampleSet& MatchingSamples);

    HRESULT HashOffLimitSamples(ITableOutput& output, SampleSet& MatchingSamples);

    HRESULT CollectMatchingSamples(const OutputSpec& output, SampleSet& MatchingSamples);

    HRESULT FindMatchingSamples();

    HRESULT RegFlushKeys();

public:
    static LPCWSTR ToolName() { return L"GetThis"; }
    static LPCWSTR ToolDescription() { return L"Sample collection"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#GETTHIS_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#GETTHIS_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , config()
        , FileFinder() {};

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetSchemaFromConfig(const ConfigItem&);

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();
};
}  // namespace Command::GetThis
}  // namespace Orc
