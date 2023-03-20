//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "UtilitiesMain.h"

#include <optional>

#include <boost/logic/tribool.hpp>

#include "OrcCommand.h"
#include "Location.h"
#include "LocationOutput.h"
#include "VolumeReader.h"
#include "MFTWalker.h"
#include "NtfsFileInfo.h"
#include "Authenticode.h"
#include "Configuration/ShadowsParserOption.h"

#pragma managed(push, off)

namespace Orc {

class DataAttribute;
class MftRecordAttribute;
class AttributeListEntry;

namespace Command::NTFSInfo {

enum KindOfTime : DWORD
{
    InvalidKind = 0,
    CreationTime = 1,
    LastModificationTime = 1 << 1,
    LastAccessTime = 1 << 2,
    LastChangeTime = 1 << 3,
    FileNameCreationDate = 1 << 4,
    FileNameLastModificationDate = 1 << 5,
    FileNameLastAccessDate = 1 << 6,
    FileNameLastAttrModificationDate = 1 << 7
};

class ORCUTILS_API Main : public UtilitiesMain
{
public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
            : locs()
        {
            bGetKnownLocations = false;
            resurrectRecordsMode = ResurrectRecordsMode::kNo;
            bAddShadows = boost::logic::indeterminate;
            bPopSystemObjects = boost::logic::indeterminate;
            ColumnIntentions = Intentions::FILEINFO_NONE;
            DefaultIntentions = Intentions::FILEINFO_NONE;

            outFileInfo.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory | OutputSpec::Kind::Archive);
            volumesStatsOutput.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::Directory | OutputSpec::Kind::TableFile | OutputSpec::Kind::Archive);
            outTimeLine.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory | OutputSpec::Kind::Archive);
            outAttrInfo.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory | OutputSpec::Kind::Archive);
            outI30Info.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory | OutputSpec::Kind::Archive);
            outSecDescrInfo.supportedTypes = static_cast<OutputSpec::Kind>(
                OutputSpec::Kind::TableFile | OutputSpec::Kind::Directory | OutputSpec::Kind::Archive);
        };

        std::wstring strWalker;

        // Output Specification
        OutputSpec outFileInfo;
        OutputSpec volumesStatsOutput;

        OutputSpec outTimeLine;
        OutputSpec outAttrInfo;
        OutputSpec outI30Info;
        OutputSpec outSecDescrInfo;

        boost::logic::tribool bGetKnownLocations;

        LocationSet locs;

        ResurrectRecordsMode resurrectRecordsMode;
        boost::logic::tribool bAddShadows;
        std::optional<LocationSet::ShadowFilters> m_shadows;
        std::optional<Ntfs::ShadowCopy::ParserType> m_shadowsParser;
        boost::logic::tribool bPopSystemObjects;
        std::optional<LocationSet::PathExcludes> m_excludes;

        Intentions ColumnIntentions;
        Intentions DefaultIntentions;
        std::vector<Filter> Filters;
    };

private:
    Configuration config;

    MultipleOutput<LocationOutput> m_FileInfoOutput;
    MultipleOutput<LocationOutput> m_VolStatOutput;

    MultipleOutput<LocationOutput> m_TimeLineOutput;
    MultipleOutput<LocationOutput> m_AttrOutput;
    MultipleOutput<LocationOutput> m_I30Output;
    MultipleOutput<LocationOutput> m_SecDescrOutput;

    MFTWalker::FullNameBuilder m_FullNameBuilder;
    DWORD dwTotalFileTreated;
    DWORD m_dwProgress;

    std::shared_ptr<AuthenticodeCache> m_authenticodeCache;
    Authenticode m_codeVerifier;

    HRESULT Prepare();
    HRESULT GetWriters(std::vector<std::shared_ptr<Location>>& locs);
    HRESULT WriteTimeLineEntry(
        ITableOutput& pTimelineOutput,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName,
        DWORD dwKind,
        LONGLONG llTime);

    HRESULT WriteTimeLineEntry(
        ITableOutput& pTimelineOutput,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName,
        DWORD dwKind,
        FILETIME time);

    std::wstring GetWalkerFromConfig(const ConfigItem& config);
    boost::logic::tribool GetPopulateSystemObjectsFromConfig(const ConfigItem& config);

    bool GetKnownLocationFromConfig(const ConfigItem& config);

    HRESULT RunThroughUSNJournal();
    HRESULT RunThroughMFT();

    // USN Walkercallback
    void USNInformation(
        const std::shared_ptr<TableOutput::IWriter>& pWriter,
        const std::shared_ptr<VolumeReader>& volreader,
        WCHAR* szFullName,
        PUSN_RECORD pRecord);

    // MFT Walker call backs
    void DisplayProgress(const ULONG dwProgress);
    void ElementInformation(ITableOutput& output, const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt);
    void DirectoryInformation(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName,
        const std::shared_ptr<IndexAllocationAttribute>& pAttr);
    void FileAndDataInformation(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName,
        const std::shared_ptr<DataAttribute>& pDataAttr);
    void AttrInformation(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const AttributeListEntry& pAttr);
    void I30Information(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PINDEX_ENTRY pEntry,
        PFILE_NAME pFileName,
        bool bCarvedEntry);
    void TimelineInformation(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName);
    void SecurityDescriptorInformation(
        ITableOutput& output,
        const std::shared_ptr<VolumeReader>& volreader,
        const PSECURITY_DESCRIPTOR_ENTRY pEntry);

public:
    static LPCWSTR ToolName() { return L"NTFSInfo"; }
    static LPCWSTR ToolDescription() { return L"NTFS File system enumeration"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#NTFSINFO_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#NTFSINFO_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , config()
        , dwTotalFileTreated(0L)
        , m_dwProgress(0L)
        , m_codeVerifier()
        , m_FileInfoOutput()
        , m_VolStatOutput()
        , m_TimeLineOutput()
        , m_AttrOutput()
        , m_I30Output()
        , m_SecDescrOutput()
        , m_authenticodeCache(std::make_shared<AuthenticodeCache>())
    {
        m_codeVerifier.SetCache(m_authenticodeCache);
    }

    // implemented in NTFSInfo_Output.cpp
    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetColumnsAndFiltersFromConfig(const ConfigItem& configitems);
    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);
    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support
    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();
};

}  // namespace Command::NTFSInfo
}  // namespace Orc

#pragma managed(pop)
