//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcCommand.h"

#include "UtilitiesMain.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class MFTRecord;
class MftRecordAttribute;

namespace Command::NTFSUtil {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    typedef enum _Command
    {
        EnumLocations,
        USN,
        Record,
        DetailLocation,
        Find,
        HexDump,
        Vss,
        MFT
    } Command;

    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
        {
            bConfigure = false;
            dwlMaxSize = 0;
            dwlMinSize = 0;
            dwlAllocDelta = 0;
            bPrintDetails = false;
            output.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::SQL);
        };

        Command cmd;
        std::wstring strVolume;

        // Record
        ULONGLONG ullRecord;

        // USN
        bool bConfigure;
        DWORDLONG dwlMaxSize;
        DWORDLONG dwlMinSize;
        DWORDLONG dwlAllocDelta;

        // HexDump
        DWORDLONG dwlOffset;
        DWORDLONG dwlSize;

        bool bPrintDetails;

        // output for vss
        OutputSpec output;
    };

private:
    Configuration config;

    HANDLE OpenVolume(LPCWSTR szVolumePath);

    // USN Journal Command
    HRESULT GetUSNJournalConfiguration(HANDLE hVolume, DWORDLONG& MaximumSize, DWORDLONG& AllocationDelta);
    HRESULT PrintUSNJournalConfiguration(HANDLE hVolume);
    HRESULT ConfigureUSNJournal(HANDLE hVolume);

    // Record command
    HRESULT PrintRecordDetails(const std::shared_ptr<VolumeReader>& volReader, MFTRecord* pRecord);
    HRESULT PrintNonResidentAttributeDetails(
        const std::shared_ptr<VolumeReader>& volReader,
        MFTRecord* pRecord,
        const std::shared_ptr<MftRecordAttribute>& pAttr,
        DWORD dwIndex);
    HRESULT PrintRecordDetails(ULONGLONG ullRecord);

    // Find record
    HRESULT FindRecord(const std::wstring& strTerm);

    // Location command
    HRESULT PrintMasterFileDetails();

    // Location command
    HRESULT PrintLocationDetails();

    // Enumerate possible locations
    HRESULT PrintAllLocations();

    // HexDump
    HRESULT PrintHexDump(DWORDLONG dwlOffset, DWORDLONG dwlSize);

    // Vss
    HRESULT PrintVss();

public:
    static LPCWSTR ToolName() { return L"NTFSUtil"; }
    static LPCWSTR ToolDescription() { return L"Various NTFS related utilities"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return nullptr; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#NTFSUTIL_SQLSCHEMA"; }

    Main(logger pLog)
        : UtilitiesMain(std::move(pLog))
    {
    }

    void PrintUsage();

    void PrintParameters();
    void PrintFooter();

    HRESULT CheckConfiguration();

    const std::wstring& GetVolumePath() const { return config.strVolume; };

    bool ShouldConfigureUSNJournal() const { return config.cmd == USN ? config.bConfigure : false; };

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem) { return S_OK; };  // No Configuration support
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT Run();
};
}  // namespace Command::NTFSUtil
}  // namespace Orc
#pragma managed(pop)
