//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcCommand.h"

#include "UtilitiesMain.h"

#include "Text/Fmt/Offset.h"

#pragma managed(push, off)

namespace Orc {

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
        BitLocker,
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
            output.supportedTypes = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile);
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

        // HexDump || BitLocker
        DWORDLONG dwlOffset;
        DWORDLONG dwlSize;

        bool bDump;

        bool bPrintDetails;

        // output for vss
        OutputSpec output;
    };

private:
    Configuration config;

    HRESULT CommandUSN();
    HRESULT CommandEnum();

    // Record command
    HRESULT PrintNonResidentAttributeDetails(
        const std::shared_ptr<VolumeReader>& volReader,
        MFTRecord* pRecord,
        const std::shared_ptr<MftRecordAttribute>& pAttr,
        DWORD dwIndex);
    HRESULT CommandRecord(ULONGLONG ullRecord);

    // Find record
    HRESULT FindRecord(const std::wstring& strTerm);

    // Location command
    HRESULT CommandMFT();

    // Location command
    HRESULT CommandLoc();

    // Enumerate possible locations
    HRESULT PrintAllLocations();

    // HexDump
    HRESULT CommandHexDump(DWORDLONG dwlOffset, DWORDLONG dwlSize);

    // Vss
    HRESULT CommandVss();

    // BitLocker
    HRESULT CommandBitLocker();

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

    Main()
        : UtilitiesMain()
    {
    }

    void PrintUsage();

    void PrintParameters();
    void PrintFooter();

    HRESULT CheckConfiguration();

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
