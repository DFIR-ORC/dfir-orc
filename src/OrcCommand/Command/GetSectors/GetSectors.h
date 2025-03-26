//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jeremie Christin
//
#pragma once

#include "OrcCommand.h"
#include "UtilitiesMain.h"
#include "ArchiveCreate.h"
#include "PartitionTable.h"
#include "TableOutput.h"
#include "TableOutputWriter.h"
#include "DiskChunkStream.h"

#pragma managed(push, off)

constexpr auto SLACK_SPACE_DUMP_SIZE = 5242880;
constexpr auto MBR_SIZE_IN_BYTES = 512;
constexpr auto VBR_SIZE_IN_BYTES = 512;
constexpr auto IPL_SIZE_IN_SECTORS = 15;
constexpr auto DEFAULT_SAMPLE_SIZE_IN_BYTES = 512;
constexpr auto EFI_PARTITION_MAX_SIZE = 419430400;  // 400 Mo

namespace Orc::Command::GetSectors {

class ORCUTILS_API Main : public UtilitiesMain
{

public:
    class Configuration : public UtilitiesMain::Configuration
    {

    public:
        std::wstring diskName;
        DWORDLONG slackSpaceDumpSize;
        OutputSpec Output;

        // Indicate if we use the setupAPI functions to get a low device interface
        bool lowInterface = true;

        // Predefined logics to dump data
        bool dumpLegacyBootCode = false;
        bool dumpSlackSpace = false;
        bool dumpUefiFull = false;
        DWORDLONG uefiFullMaxSize = 0;

        // Specific data to dump
        bool customSample = false;
        ULONGLONG customSampleOffset = 0;
        DWORD customSampleSize = 0;

        Configuration()
        {
            slackSpaceDumpSize = SLACK_SPACE_DUMP_SIZE;
            customSampleSize = DEFAULT_SAMPLE_SIZE_IN_BYTES;
            uefiFullMaxSize = EFI_PARTITION_MAX_SIZE;
        };
    };

private:
    Configuration config;

    class DiskChunk
    {

    public:
        // Name of a device that identify the disk. Also the name used when writing results in output.
        std::wstring m_DiskName;
        // Name of a disk device interface to used when reading the disk. By default, equal to m_DiskName.
        std::wstring m_DiskInterface;
        ULONGLONG m_ulChunkOffset;
        DWORD m_ulChunkSize;
        ULONGLONG m_readingTime = 0;
        std::wstring m_description;
        CBinaryBuffer m_cBuf;
        boolean m_sectorBySectorMode = true;

        DiskChunk(
            std::wstring diskName,
            ULONGLONG offset,
            DWORD size,
            std::wstring desc,
            std::wstring diskInterface = L"")
            : m_DiskName(diskName)
            , m_DiskInterface(diskName)
            , m_ulChunkOffset(offset)
            , m_ulChunkSize(size)
            , m_description(desc)
        {
            if (diskInterface.empty())
            {
                m_DiskInterface = diskName;
            }
            else
            {
                m_DiskInterface = diskInterface;
            }
        }

        HRESULT read();
        void setSectorBySectorMode(boolean mode) { m_sectorBySectorMode = mode; };
        std::wstring getSampleName();
    };

    // Variable used to accumulate dumped chunks that will be archived at the end
    std::vector<std::shared_ptr<DiskChunkStream>> dumpedChunks;

    std::wstring getBootDiskName();
    int64_t getBootDiskSignature();
    int64_t getDiskSignature(const std::wstring& diskName);
    std::wstring identifyLowDiskInterfaceByMbrSignature(const std::wstring& diskName);
    HRESULT DumpSlackSpace(const std::wstring& diskName, const std::wstring& diskInterfaceToRead = L"");
    HRESULT DumpBootCode(const std::wstring& diskName, const std::wstring& diskInterfaceToRead = L"");
    HRESULT DumpRawGPT(const std::wstring& diskName, const std::wstring& diskInterfaceToRead = L"");
    HRESULT DumpCustomSample(const std::wstring& diskName, const std::wstring& diskInterfaceToRead = L"");
    HRESULT CollectDiskChunks(const OutputSpec& output);

    std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
    CreateDiskChunkArchiveAndCSV(const std::wstring& pArchivePath, const std::shared_ptr<ArchiveCreate>& compressor);
    std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
    CreateOutputDirLogFileAndCSV(const std::wstring& pDirPath);

    HRESULT
    FinalizeArchive(const std::shared_ptr<ArchiveCreate>& compressor, const std::shared_ptr<TableOutput::IWriter>& CSV);

    HRESULT CollectDiskChunk(
        const std::shared_ptr<ArchiveCreate>& compressor,
        ITableOutput& output,
        std::shared_ptr<DiskChunkStream> diskChunk);
    HRESULT
    CollectDiskChunk(const std::wstring& strOutDir, ITableOutput& output, std::shared_ptr<DiskChunkStream> diskChunk);
    HRESULT AddDiskChunkRefToCSV(ITableOutput& output, const std::wstring& strComputerName, DiskChunkStream& diskChunk);
    bool extractInfoFromLocation(
        const std::shared_ptr<Location>& location,
        std::wstring& out_deviceName,
        ULONGLONG& out_offset,
        ULONGLONG& out_size);
    std::shared_ptr<DiskChunkStream>
    readAtVolumeLevel(const std::wstring& diskName, ULONGLONG offset, DWORD size, const std::wstring& description);

public:
    static LPCWSTR ToolName() { return L"GetSectors"; }
    static LPCWSTR ToolDescription() { return L"Boot sectors, slack space collection"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return nullptr; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#GETSECTORS_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
    {
    }

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetSchemaFromConfig(const ConfigItem&);

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem) { return S_OK; };  // No Configuration support
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);
    HRESULT CheckConfiguration();
    HRESULT Run();
};
}  // namespace Orc::Command::GetSectors
