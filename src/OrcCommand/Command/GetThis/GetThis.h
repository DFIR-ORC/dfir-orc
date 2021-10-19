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

#include <filesystem>
#include <vector>
#include <set>
#include <string>

#include <boost/logic/tribool.hpp>

#include "Configuration/ConfigFileReader.h"
#include "Configuration/ConfigFileReader.h"
#include "MFTWalker.h"
#include "Location.h"
#include "ArchiveCreate.h"
#include "FileFind.h"
#include "TableOutputWriter.h"

#include "ByteStream.h"
#include "OrcLimits.h"

#include "CryptoHashStream.h"
#include "FuzzyHashStream.h"

#include "Archive/Appender.h"
#include "Archive/7z/Archive7z.h"

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
    GlobalMaxTotalBytes = 1 << 2,

    LocalSampleCountLimitReached = 1 << 3,
    LocalMaxBytesPerSample = 1 << 4,
    LocalMaxTotalBytes = 1 << 5,

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

        LONGLONG VolumeSerial;
        MFT_SEGMENT_REFERENCE FRN;
        USHORT InstanceID;
        size_t AttributeIndex = 0;
        ContentSpec Content;
        std::shared_ptr<CryptoHashStream> HashStream;
        std::shared_ptr<FuzzyHashStream> FuzzyHashStream;
        std::shared_ptr<ByteStream> CopyStream;
        GUID SnapshotID;
        LimitStatus LimitStatus;
        std::wstring SourcePath;

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
            std::swap(LimitStatus, Other.LimitStatus);
            std::swap(Matches, Other.Matches);
            AttributeIndex = Other.AttributeIndex;
            InstanceID = Other.InstanceID;
            std::swap(HashStream, Other.HashStream);
            std::swap(CopyStream, Other.CopyStream);
            std::swap(Content, Other.Content);
            std::swap(SnapshotID, Other.SnapshotID);
            std::swap(SourcePath, Other.SourcePath);
        }

        bool IsOfflimits() const
        {
            switch (LimitStatus)
            {
                case NoLimits:
                case SampleWithinLimits:
                    return false;
                case GlobalSampleCountLimitReached:
                case GlobalMaxBytesPerSample:
                case GlobalMaxTotalBytes:
                case LocalSampleCountLimitReached:
                case LocalMaxBytesPerSample:
                case LocalMaxTotalBytes:
                case FailedToComputeLimits:
                    return true;
                default:
                    _ASSERT("Unhandled 'LimitStatus' case");
            }

            return true;
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

    struct SampleId
    {
        explicit SampleId(const SampleRef& sample)
            : FRN(sample.FRN)
            , AttributeIndex(sample.AttributeIndex)
            , VolumeSerial(sample.VolumeSerial)
            , SnapshotId(sample.SnapshotID)
        {
        }

        const MFT_SEGMENT_REFERENCE FRN;
        const size_t AttributeIndex;
        const LONGLONG VolumeSerial;
        const GUID SnapshotId;
    };

    struct SampleIdHasher
    {
        size_t operator()(const SampleId& sampleId) const { return sampleId.FRN.SegmentNumberLowPart; }
    };

    struct SampleIdComparator
    {
        bool operator()(const SampleId& lhs, const SampleId& rhs) const
        {
            // Low first because of its higher probability of not matching
            if (lhs.FRN.SegmentNumberLowPart != rhs.FRN.SegmentNumberLowPart)
            {
                return false;
            }

            if (lhs.FRN.SegmentNumberHighPart != rhs.FRN.SegmentNumberHighPart)
            {
                return false;
            }

            if (lhs.FRN.SequenceNumber != rhs.FRN.SequenceNumber)
            {
                return false;
            }

            if (lhs.AttributeIndex != rhs.AttributeIndex)
            {
                return false;
            }

            if (lhs.VolumeSerial != rhs.VolumeSerial)
            {
                return false;
            }

            if (memcmp(&lhs.SnapshotId, &rhs.SnapshotId, sizeof(GUID)) != 0)
            {
                return false;
            }

            return true;
        }
    };

private:
    Configuration config;

    using SampleIds = std::unordered_set<SampleId, SampleIdHasher, SampleIdComparator>;
    SampleIds m_sampleIds;

    FileFind FileFinder;
    FILETIME CollectionDate;
    const std::wstring ComputerName;
    Limits GlobalLimits;
    std::unique_ptr<Archive::Appender<Archive::Archive7z>> m_compressor;
    std::shared_ptr<Orc::TableOutput::IStreamWriter> m_tableWriter;

    HRESULT ConfigureSampleStreams(SampleRef& sample) const;

    HRESULT AddSampleRefToCSV(ITableOutput& output, const SampleRef& sampleRef) const;

    std::unique_ptr<SampleRef> CreateSample(
        const std::shared_ptr<FileFind::Match>& match,
        const size_t attributeIndex,
        const SampleSpec& sampleSpec) const;

    using SampleWrittenCb = std::function<void(const SampleRef&, HRESULT hrWrite)>;

    HRESULT WriteSample(
        Archive::Appender<Archive::Archive7z>& compressor,
        std::unique_ptr<SampleRef> pSample,
        SampleWrittenCb writtenCb = {}) const;

    HRESULT WriteSample(
        const std::filesystem::path& outputDir,
        std::unique_ptr<SampleRef> sample,
        SampleWrittenCb writtenCb = {}) const;

    void UpdateSamplesLimits(SampleSpec& sampleSpec, const SampleRef& sample);

    void FinalizeHashes(const Main::SampleRef& sample) const;

    HRESULT FindMatchingSamples();

    void OnMatchingSample(const std::shared_ptr<FileFind::Match>& aMatch, bool bStop);
    void OnSampleWritten(const SampleRef& sample, const SampleSpec& sampleSpec, HRESULT hrWrite) const;

public:
    Main();

    static LPCWSTR ToolName() { return L"GetThis"; }
    static LPCWSTR ToolDescription() { return L"Sample collection"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#GETTHIS_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#GETTHIS_SQLSCHEMA"; }

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

private:
    HRESULT InitOutput();
    HRESULT CloseOutput();

    HRESULT InitArchiveOutput();
    HRESULT InitDirectoryOutput();

    HRESULT CloseArchiveOutput();
    HRESULT CloseDirectoryOutput();
};

}  // namespace Command::GetThis

}  // namespace Orc
