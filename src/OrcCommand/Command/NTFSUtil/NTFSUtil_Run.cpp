//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include <memory>
#include <sstream>

#include <boost/scope_exit.hpp>

#include "TableOutputWriter.h"
#include "NTFSUtil.h"
#include "MFTWalker.h"
#include "VolumeReader.h"
#include "PartitionTable.h"
#include "Location.h"
#include "VolumeShadowCopies.h"
#include "BitLocker.h"
#include "MFTWalker.h"
#include "MFTOnline.h"
#include "MFTOffline.h"
#include "OfflineMFTReader.h"

#include "Utils/TypeTraits.h"
#include "Text/Fmt/Boolean.h"
#include "Text/Fmt/Limit.h"
#include "Text/Fmt/Partition.h"
#include "Text/Fmt/PartitionFlags.h"
#include "Text/Fmt/PartitionType.h"
#include "Text/Format.h"
#include "Text/Print/LocationSet.h"
#include "Text/Print/Ntfs.h"
#include "Text/Guid.h"
#include "Text/HexDump.h"
#include "Text/Hex.h"
#include "Utils/Guard.h"
#include "Utils/WinApi.h"
#include "Text/Fmt/GUID.h"
#include "Text/Fmt/FILETIME.h"
#include "Filesystem/Ntfs/ShadowCopy/Dump.h"
#include "Filesystem/Ntfs/ShadowCopy/ShadowCopy.h"
#include "Filesystem/Ntfs/ShadowCopy/ShadowCopyStream.h"
#include "Filesystem/Ntfs/ShadowCopy/Snapshot.h"
#include "Filesystem/Ntfs/ShadowCopy/SnapshotsIndex.h"
#include "Filesystem/Ntfs/ShadowCopy/SnapshotsIndexHeader.h"
#include "ShadowCopyVolumeReader.h"
#include "Stream/VolumeStreamReader.h"

using namespace std;

using namespace Orc::Command::NTFSUtil;
using namespace Orc::Text;
using namespace Orc;

namespace {

HANDLE OpenVolume(const std::wstring& volume)
{
    HANDLE hVolume = INVALID_HANDLE_VALUE;

    hVolume = CreateFile(
        volume.c_str(),
        GENERIC_READ,
        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0L,
        NULL);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        Log::Debug(L"Failed to open volume: '{}' [{}]", volume, LastWin32Error());
        return INVALID_HANDLE_VALUE;
    }

    return hVolume;
}

HRESULT GetUSNJournalConfiguration(HANDLE hVolume, DWORDLONG& maximumSize, DWORDLONG& allocationDelta)
{
    DWORD dwBytesReturned = 0L;
    USN_JOURNAL_DATA journalData;
    ZeroMemory(&journalData, sizeof(journalData));

    BOOL ret = DeviceIoControl(
        hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0L, &journalData, sizeof(journalData), &dwBytesReturned, NULL);
    if (ret == FALSE)
    {
        const DWORD dwErr = GetLastError();
        const HRESULT hr = HRESULT_FROM_WIN32(dwErr);

        switch (dwErr)
        {
            case ERROR_JOURNAL_DELETE_IN_PROGRESS:
                Log::Error("An attempt is made to read the journal while a journal deletion is in process");
                break;
            case ERROR_JOURNAL_NOT_ACTIVE:
                Log::Error("An attempt is made to read USN journal while being inactive");
                break;
            default:
                Log::Error("Failed FSCTL_QUERY_USN_JOURNAL [{}]", SystemError(hr));
        }

        return hr;
    }

    maximumSize = journalData.MaximumSize;
    allocationDelta = journalData.AllocationDelta;
    return S_OK;
}

HRESULT ConfigureUSNJournal(HANDLE hVolume, DWORDLONG dwlMinSize, DWORDLONG dwlMaxSize, DWORDLONG dwlAllocDelta)
{
    HRESULT hr = E_FAIL;

    if (dwlMinSize > 0)
    {
        DWORDLONG MaxSize = 0;
        DWORDLONG Delta = 0;

        hr = ::GetUSNJournalConfiguration(hVolume, MaxSize, Delta);
        if (FAILED(hr))
        {
            return hr;
        }

        if (MaxSize >= dwlMinSize)
        {
            Log::Error("Cannot shrink USN journal because specified size is smaller than the current");
            return E_FAIL;
        }

        dwlMaxSize = dwlMinSize;
    }

    CREATE_USN_JOURNAL_DATA JournalCreateData = {dwlMaxSize, dwlAllocDelta};
    DWORD dwBytesReturned = 0L;

    if (!DeviceIoControl(
            hVolume,
            FSCTL_CREATE_USN_JOURNAL,
            &JournalCreateData,
            sizeof(JournalCreateData),
            NULL,
            0L,
            &dwBytesReturned,
            NULL))
    {
        const DWORD dwErr = GetLastError();
        hr = HRESULT_FROM_WIN32(dwErr);

        switch (dwErr)
        {
            case ERROR_JOURNAL_DELETE_IN_PROGRESS:
                Log::Error("An attempt is made to modify the journal while a journal deletion is in process");
                break;
            default:
                Log::Error("Failed FSCTL_QUERY_USN_JOURNAL [{}]", SystemError(hr));
        }

        return hr;
    }

    return S_OK;
}

std::string ToApplicationInfoIdString(const GUID& guid)
{
    using namespace Orc::Ntfs::ShadowCopy;

    if (guid == ApplicationInformation::VssLocalInfo::kMicrosoftGuid)
    {
        return ToString(guid) + " [Microsoft]";
    }
    else if (guid == ApplicationInformation::VssLocalInfo::kMicrosoftHiddenGuid)
    {
        return ToString(guid) + " [Microsoft hidden]";
    }
    else
    {
        return ToString(guid) + " [Unusual guid]";
    }
}

}  // namespace

HRESULT Main::CommandUSN()
{
    HRESULT hr = E_FAIL;
    auto output = m_console.OutputTree();

    Guard::FileHandle hVolume = OpenVolume(config.strVolume);
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        Log::Critical(L"Failed to open volume: '{}' [{}]", config.strVolume, SystemError(hr));
        return E_FAIL;
    }

    {
        auto maxSize = Traits::ByteQuantity<DWORDLONG>(0);
        auto delta = Traits::ByteQuantity<DWORDLONG>(0);

        hr = ::GetUSNJournalConfiguration(*hVolume, maxSize, delta);
        if (FAILED(hr))
        {
            Log::Critical(
                L"Failed to obtain USN configuration values for volume: '{}' [{}]", config.strVolume, SystemError(hr));
            return hr;
        }

        auto usnNode = output.AddNode("USN configuration:");
        PrintValue(usnNode, L"MaxSize", maxSize);
        PrintValue(usnNode, L"Delta", delta);
    }

    if (config.bConfigure)
    {
        hr = ::ConfigureUSNJournal(*hVolume, config.dwlMinSize, config.dwlMaxSize, config.dwlAllocDelta);
        if (FAILED(hr))
        {
            Log::Critical(L"Failed to configure USN journal for volume '{}' [{}]", config.strVolume, SystemError(hr));
            return hr;
        }

        {
            auto maxSize = Traits::ByteQuantity<DWORDLONG>(0);
            auto delta = Traits::ByteQuantity<DWORDLONG>(0);

            hr = ::GetUSNJournalConfiguration(*hVolume, maxSize, delta);
            if (FAILED(hr))
            {
                Log::Critical(
                    L"Failed to obtain new USN configuration values for volume: '{}' [{}]",
                    config.strVolume,
                    SystemError(hr));
                return hr;
            }

            auto usnNode = output.AddNode("USN new configuration:");
            PrintValue(usnNode, L"MaxSize", maxSize);
            PrintValue(usnNode, L"Delta", delta);
        }
    }

    output.AddEmptyLine();
    return S_OK;
}

HRESULT Main::CommandEnum()
{
    auto output = m_console.OutputTree();

    LocationSet locations;

    HRESULT hr = locations.EnumerateLocations();
    if (FAILED(hr))
    {
        Log::Error("Failed to enumerate locations [{}]", SystemError(hr));
        return hr;
    }

    if (!config.strVolume.empty())
    {
        std::vector<std::shared_ptr<Location>> addedLocs;
        HRESULT hr = locations.AddLocations(config.strVolume.c_str(), addedLocs);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to add locations from '{}' [{}]", config.strVolume, SystemError(hr));
            return hr;
        }
    }

    locations.Consolidate(true, FSVBR::FSType::ALL);
    Orc::Text::PrintValue(output, L"Available locations", locations);

    output.AddEmptyLine();
    return S_OK;
}

HRESULT Main::CommandLoc()
{
    Location::Type locationType = Location::Type::Undetermined;
    std::wstring canonical, subPath;
    LocationSet locationSet;

    HRESULT hr = locationSet.CanonicalizeLocation(config.strVolume.c_str(), canonical, locationType, subPath);
    if (FAILED(hr))
    {
        return hr;
    }

    auto root = m_console.OutputTree();

    auto output = root.AddNode(L"Location '{}'", config.strVolume);
    PrintValue(output, L"Type", locationType);

    if (locationType == Location::Type::ImageFileDisk || locationType == Location::Type::PhysicalDrive
        || locationType == Location::Type::DiskInterface)
    {
        PartitionTable pt;
        hr = pt.LoadPartitionTable(canonical.c_str());
        if (FAILED(hr))
        {
            Log::Error(L"Failed to load partition table for '{}' [{}]", canonical, SystemError(hr));
            return hr;
        }

        PrintValues(output, L"Partitions", pt.Table());
    }

    std::vector<std::shared_ptr<Location>> addedLocations;

    hr = locationSet.AddLocations(config.strVolume.c_str(), addedLocations, false);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to parse location '{}' [{}]", config.strVolume, SystemError(hr));
        return hr;
    }

    auto partitionsNode = output.AddNode("Filesystems:");
    for (const auto& location : addedLocations)
    {
        auto partitionNode = partitionsNode.AddNode("{}", location->GetLocation());

        auto reader = location->GetReader();
        if (reader == nullptr)
        {
            Log::Debug(L"Failed to retrieve reader for '{}' [{}]", location->GetLocation(), SystemError(hr));
            continue;
        }

        HRESULT hr = reader->LoadDiskProperties();
        if (FAILED(hr))
        {
            Log::Debug(L"Failed to load disk properties for '{}' [{}]", location->GetLocation(), SystemError(hr));
            continue;
        }

        if (location->IsNTFS())
        {
            PrintValue(partitionNode, L"FS type", "NTFS");
            PrintValue(partitionNode, L"Volume serial", fmt::format(L"{:#x}", reader->VolumeSerialNumber()));
            PrintValue(partitionNode, L"Component length", reader->MaxComponentLength());
            PrintValue(partitionNode, L"Bytes per cluster", reader->GetBytesPerCluster());
            PrintValue(partitionNode, L"Bytes per FRS", reader->GetBytesPerFRS());

            MFTWalker walker;
            hr = walker.Initialize(location);
            if (FAILED(hr))
            {
                Log::Debug(L"Failed to parse MFT for '{}' [{}]", location->GetLocation(), SystemError(hr));
                continue;
            }

            PrintValue(partitionNode, L"Record count", walker.GetMFTRecordCount());
        }
        else if (location->IsFAT12() || location->IsFAT16() || location->IsFAT32())
        {
            if (location->IsFAT12())
            {
                PrintValue(partitionNode, L"FS type", "FAT12");
            }
            else if (location->IsFAT16())
            {
                PrintValue(partitionNode, L"FS type", "FAT16");
            }
            else
            {
                PrintValue(partitionNode, L"FS type", "FAT32");
            }

            PrintValue(partitionNode, L"Bytes per cluster", reader->GetBytesPerCluster());
            PrintValue(partitionNode, L"Volume serial", fmt::format(L"{:#x}", reader->VolumeSerialNumber()));
        }
    }

    root.AddEmptyLine();
    return S_OK;
}

// Record command
HRESULT Main::CommandRecord(ULONGLONG ullRecord)
{
    auto root = m_console.OutputTree();

    LocationSet locs;

    std::vector<std::shared_ptr<Location>> addedLocs;

    HRESULT hr = locs.AddLocations(config.strVolume.c_str(), addedLocs);
    if (FAILED(hr))
    {
        return hr;
    }

    auto loc = addedLocs[0];

    if (loc->GetReader() == nullptr)
    {
        Log::Error(L"Unable to instantiate Volume reader for location: '{}'", loc->GetLocation());
        return E_FAIL;
    }

    hr = loc->GetReader()->LoadDiskProperties();
    if (FAILED(hr))
    {
        Log::Error(L"Unable to load disk properties for location: '{}' [{}]", loc->GetLocation(), SystemError(hr));
        return hr;
    }

    if (!loc->IsNTFS())
    {
        Log::Error(L"Record details command is only available on a NTFS volume: '{}'", config.strVolume);
        return hr;
    }

    MFTWalker walk;
    hr = walk.Initialize(addedLocs[0], ResurrectRecordsMode::kYes);
    if (FAILED(hr))
    {
        Log::Error(L"Failed during MFT walk initialisation on volume: '{}'", config.strVolume);
        return hr;
    }

    MFTWalker::Callbacks callbacks;

    bool bRecordFound = false;

    callbacks.ElementCallback = [ullRecord, this, &hr, &root, &bRecordFound](
                                    const std::shared_ptr<VolumeReader>& volReader, MFTRecord* pRecord) {
        if (ullRecord == pRecord->GetSafeMFTSegmentNumber() || ullRecord == pRecord->GetUnSafeMFTSegmentNumber())
        {
            Print(root, *pRecord, volReader);
            bRecordFound = true;
        }
    };

    callbacks.ProgressCallback = [&bRecordFound, this](ULONG dwProgress) -> HRESULT {
        if (bRecordFound)
        {
            Log::Info("Record found, stopping enumeration");
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES);
        }
        return S_OK;
    };

    hr = walk.Walk(callbacks);
    if (FAILED(hr))
    {
        if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
        {
            Log::Debug("Enumeration is stopping");
        }
        else
        {
            Log::Error("Failed during MFT walk on volume [{}]", SystemError(hr));
        }
    }

    root.AddEmptyLine();
    return S_OK;
}

// Find record
HRESULT Main::FindRecord(const std::wstring& strTerm)
{
    return S_OK;
}

HRESULT Main::CommandMFT()
{
    auto root = m_console.OutputTree();

    LocationSet locs;

    std::vector<std::shared_ptr<Location>> addedLocs;
    HRESULT hr = locs.AddLocations(config.strVolume.c_str(), addedLocs);
    if (FAILED(hr))
    {
        return hr;
    }

    locs.Consolidate(false, FSVBR::FSType::NTFS | FSVBR::FSType::BITLOCKER);

    for (const auto& loc : locs.GetAltitudeLocations())
    {
        auto volreader = loc->GetReader();
        if (volreader == nullptr)
        {
            Log::Error(L"Unable to instantiate Volume reader for location: '{}'", loc->GetLocation());
            continue;
        }

        hr = volreader->LoadDiskProperties();
        if (FAILED(hr))
        {
            Log::Error(L"Unable to load disk properties for location: '{}' [{}]", loc->GetLocation(), SystemError(hr));
            continue;
        }

        if (!loc->IsNTFS())
        {
            Log::Error(L"MFT details command is only available for NTFS volumes: '{}'", config.strVolume);
            continue;
        }

        if (Location::Type::OfflineMFT == loc->GetType())
        {
            // fabienfl: there was no output, just initialization
            Log::Error("Offline MFT volume is not supported");
            continue;
        }

        auto pMFT = std::make_unique<MFTOnline>(loc->GetReader());
        if (FAILED(pMFT->Initialize()))
        {
            return hr;
        }

        const auto& mftinfo = pMFT->GetMftInfo();

        auto volumeNode = root.AddNode("MFT information for volume: '{}'", loc->GetLocation());

        auto extentNode = volumeNode.AddNode("Extents ({} non-resident entries)", mftinfo.ExtentsVector.size());
        for (const auto& extent : mftinfo.ExtentsVector)
        {
            Print(extentNode, extent);

            // TODO: fabienfl: I believe this should be somewhere else, it just check for FILE header and print full
            // segment number

            // if (extent.bZero)
            //{
            //    // sparse entry
            //    continue;
            //}

            // ULONGLONG ullBytesRead = 0LL;
            // CBinaryBuffer buffer;
            // buffer.SetCount(1024);

            // hr = volreader->Read(extent.DiskOffset, buffer, 1024, ullBytesRead);
            // if (FAILED(hr))
            //{
            //    Log::Error(L"Failed to read first MFT record in extent [{}]", SystemError(hr));
            //    continue;
            //}

            // const auto pData = reinterpret_cast<FILE_RECORD_SEGMENT_HEADER*>(buffer.GetData());

            // if ((pData->MultiSectorHeader.Signature[0] != 'F') || (pData->MultiSectorHeader.Signature[1] != 'I')
            //    || (pData->MultiSectorHeader.Signature[2] != 'L') || (pData->MultiSectorHeader.Signature[3] != 'E'))
            //{
            //    Log::Error(
            //        L"First MFT record of extent at {} doesn't have a file signature",
            //        Traits::Offset(extent.DiskOffset));
            //    continue;
            //}

            // ULARGE_INTEGER li {0};

            // li.LowPart = pData->SegmentNumberLowPart;
            // li.HighPart = pData->SegmentNumberHighPart;

            // Log::Info(
            //    L"\t\tFILE({:#018x}) {}",
            //    li.QuadPart,
            //    ((li.QuadPart * 1024) / volreader->GetBytesPerCluster() == extent.LowestVCN ? L"inSequence"
            //                                                                                : L"outOfSequence"));
        }

        // Print statistics about the MFT (after walking it)
        MFTWalker walker;

        hr = walker.Initialize(loc);
        if (FAILED(hr))
        {
            Log::Warn(L"Could not initialize walker for volume: '{}' [{}]", loc->GetIdentifier(), SystemError(hr));
            continue;
        }

        MFTWalker::Callbacks callbacks;
        ULONG ulCompleteRecords = 0L;

        callbacks.ElementCallback =
            [&ulCompleteRecords](const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt) {
                ulCompleteRecords++;
            };

        hr = walker.Walk(callbacks);
        if (FAILED(hr))
        {
            Log::Warn(L"Could not walk volume: '{}' [{}]", loc->GetIdentifier(), SystemError(hr));
            continue;
        }

        auto successRate = (((double)ulCompleteRecords) / ((double)pMFT->GetMFTRecordCount())) * 100;

        volumeNode.AddEmptyLine();
        volumeNode.Add(
            L"Successfully walked {} records (out of {} elements, {:.2f}%)",
            ulCompleteRecords,
            pMFT->GetMFTRecordCount(),
            successRate);
        volumeNode.AddEmptyLine();
    }

    return S_OK;
}

// Print HexDump
HRESULT Main::CommandHexDump(DWORDLONG dwlOffset, DWORDLONG dwlSize)
{
    auto output = m_console.OutputTree();
    LocationSet locs;

    std::vector<std::shared_ptr<Location>> addedLocs;
    HRESULT hr = locs.AddLocations(config.strVolume.c_str(), addedLocs);
    if (FAILED(hr))
    {
        return hr;
    }

    std::shared_ptr<VolumeReader> volreader;
    for (const auto& loc : addedLocs)
    {
        hr = loc->GetReader()->LoadDiskProperties();
        if (FAILED(hr))
        {
            Log::Error(L"Unable to load disk properties for location: '{}' [{}]", loc->GetLocation(), SystemError(hr));
            return hr;
        }

        if (loc->GetReader()->IsReady())
        {
            volreader = loc->GetReader();
            break;
        }
    }

    if (volreader == nullptr)
    {
        Log::Error(L"Failed to create a reader for volume: '{}'", config.strVolume);
        return E_INVALIDARG;
    }

    hr = volreader->LoadDiskProperties();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to load disk properties for volume: '{}' [{}]", config.strVolume, SystemError(hr));
    }

    if (volreader->GetBytesPerCluster() == 0)
    {
        Log::Error(L"Invalid bytes per cluster for volume: '{}'", config.strVolume);
        return E_FAIL;
    }

    CBinaryBuffer buffer;
    buffer.SetCount(volreader->GetBytesPerCluster());

    ULONGLONG Read = 0LL;
    size_t BigOffset = 0;

    while (Read < dwlSize)
    {
        DWORDLONG ullThisRead = 0LL;
        ZeroMemory(buffer.GetData(), buffer.GetCount());
        if (FAILED(hr = volreader->Read(dwlOffset, buffer, volreader->GetBytesPerCluster(), ullThisRead)))
        {
            Log::Error(L"Failed to read volume '{}' [{}]", config.strVolume, SystemError(hr));
            return hr;
        }

        buffer.SetCount(std::min(
            (size_t)msl::utilities::SafeInt<size_t>(ullThisRead),
            static_cast<size_t>(
                dwlSize
                - Read)));  // If left to "print" is smaller than cluster size, then only print what is necessary

        auto in = std::string_view(reinterpret_cast<char*>(buffer.GetData()), buffer.GetCount());

        // TODO: this will output multiple dump instead of a long one, it should accept an input iterator
        output.AddHexDump(
            fmt::format(L"Dump at offset: {}, length: {}", Traits::Offset(dwlOffset), Traits::ByteQuantity(dwlSize)),
            in);

        Read += ullThisRead;
    }

    return S_OK;
}

HRESULT Main::CommandVss()
{
    using namespace Ntfs::ShadowCopy;

    auto rootNode = m_console.OutputTree();

    LocationSet locations;
    locations.SetShadowCopyParser(Ntfs::ShadowCopy::ParserType::kInternal);

    HRESULT hr = locations.EnumerateLocations();
    if (FAILED(hr))
    {
        Log::Error("Failed to enumerate locations [{}]", SystemError(hr));
        return hr;
    }

    if (!config.strVolume.empty())
    {
        std::vector<std::shared_ptr<Location>> addedLocs;
        hr = locations.AddLocations(config.strVolume.c_str(), addedLocs);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to add locations from '{}' [{}]", config.strVolume, SystemError(hr));
            return hr;
        }
    }

    hr = locations.Consolidate(false, FSVBR::FSType::ALL);
    if (FAILED(hr))
    {
        Log::Error("Failed to analyze locations [{}]", SystemError(hr));
        return hr;
    }

    std::shared_ptr<TableOutput::IWriter> pVssWriter;
    if (config.output.Type != OutputSpec::Kind::None)
    {
        pVssWriter = TableOutput::GetWriter(config.output);
        if (nullptr == pVssWriter)
        {
            Log::Critical("Failed to create output file");
            return E_FAIL;
        }
    }

    if (pVssWriter != nullptr)
    {
        Guard::Scope onScopeExit([&] {
            if (pVssWriter != nullptr)
            {
                pVssWriter->Close();
            }
        });

        for (const auto& [path, location] : locations.GetLocations())
        {
            std::error_code ec;

            std::vector<VolumeShadowCopies::Shadow> shadows;
            location->EnumerateShadowCopies(shadows, {}, ec);
            if (ec)
            {
                Log::Error(L"Failed to enumerate shadow copy: {} [{}]", location->GetLocation(), ec);
                continue;
            }

            auto& output = *pVssWriter;
            for (const auto& shadow : shadows)
            {
                output.WriteGUID(shadow.guid);
                output.WriteString(shadow.DeviceInstance.c_str());
                output.WriteString(shadow.VolumeName.c_str());
                output.WriteFileTime(shadow.CreationTime);
                output.WriteFlags(shadow.Attributes, VolumeShadowCopies::g_VssFlags, L'|');
                output.WriteEndOfLine();
            }
        }
    }
    else
    {
        for (const auto& location : locations.GetAltitudeLocations())
        {
            Log::Debug(L"Check location: '{}'", location->GetLocation());

            std::error_code ec;

            auto reader = location->GetReader();
            if (!reader)
            {
                Log::Error(L"Failed to get stream reader for {}", location->GetLocation());
                ec.clear();
                continue;
            }

            hr = reader->LoadDiskProperties();
            if (FAILED(hr))
            {
                Log::Error(L"Failed to load volume reader properties for {}", location->GetLocation());
                ec.clear();
                continue;
            }

            if (reader->GetFSType() != FSVBR_FSType::NTFS)
            {
                Log::Debug("Volume is not NTFS so shadow copy support is disabled");
                continue;
            }

            auto stream = std::make_shared<VolumeStreamReader>(reader);
            if (!Ntfs::ShadowCopy::HasSnapshotsIndex(*stream, ec))
            {
                if (ec)
                {
                    Log::Error(L"Failed to check if volume has shadow copies: '{}' [{}]", location->GetLocation(), ec);
                    ec.clear();
                    continue;
                }

                continue;
            }

            SnapshotsIndexHeader snapshotsIndexHeader;
            SnapshotsIndexHeader::Parse(*stream, snapshotsIndexHeader, ec);
            if (ec)
            {
                Log::Error(L"Failed to parse snapshots index header: {} [{}]", location->GetLocation(), ec);
                ec.clear();
                continue;
            }

            Ntfs::ShadowCopy::SnapshotsIndex snapshotsIndex;
            Ntfs::ShadowCopy::SnapshotsIndex::Parse(*stream, snapshotsIndex, ec);
            if (ec)
            {
                Log::Error(L"Failed to enumerate snapshots: {} [{}]", location->GetLocation(), ec);
                ec.clear();
                continue;
            }

            auto vssNode = rootNode.AddNode(L"Volume shadow copy information");
            PrintValue(vssNode, L"Device instance", location->GetLocation());
            // PrintValue(vssNode, L"Mount point", boost::join(location->GetPaths(), L", "));
            PrintValue(vssNode, L"Volume serial", fmt::format("{:#x}", reader->VolumeSerialNumber()));
            PrintValue(vssNode, L"Volume id", snapshotsIndexHeader.VolumeGuid());
            PrintValue(vssNode, L"Storage id", snapshotsIndexHeader.StorageGuid());
            PrintValue(vssNode, L"Snapshots version", snapshotsIndexHeader.Version());
            PrintValue(vssNode, L"Locally stored", Traits::Boolean(snapshotsIndexHeader.IsLocalStorage()));
            PrintValue(vssNode, L"Has catalog", Traits::Boolean(snapshotsIndexHeader.HasCatalog()));
            PrintValue(vssNode, L"Catalog offset", Traits::Offset(snapshotsIndexHeader.FirstCatalogOffset()));
            PrintValue(vssNode, L"Maximum size", Traits::ByteQuantity(snapshotsIndexHeader.MaximumSize()));
            PrintValue(vssNode, L"Flags", fmt::format("{:#x}", snapshotsIndexHeader.Flags()));
            PrintValue(vssNode, L"Protection flags", fmt::format("{:#x}", snapshotsIndexHeader.ProtectionFlags()));

            if (snapshotsIndex.Items().empty())
            {
                Log::Debug(L"No snapshot found for '{}'", location->GetLocation());

                PrintValue(vssNode, L"Snapshot", L"<None>");
                continue;
            }

            std::set<Ntfs::ShadowCopy::VolumeSnapshotAttributes> attributes;
            auto snapshotNode = vssNode.AddNode(L"Snapshots summary:");
            for (const auto& snapshot : snapshotsIndex.Items())
            {
                Print(
                    snapshotNode,
                    fmt::format(
                        "Id: {}, Creation: {}, Flags: {}, Attributes: {}",
                        snapshot.ShadowCopyId(),
                        snapshot.CreationTime(),
                        fmt::format("{:#x}", snapshot.Flags()),
                        fmt::format("{:#x}", static_cast<uint32_t>(snapshot.VolumeSnapshotAttributes()))));
            }

            vssNode.AddEmptyLine();
            vssNode.AddEmptyLine();

            for (size_t i = 0; i < snapshotsIndex.Items().size(); ++i)
            {
                const auto& snapshot = snapshotsIndex.Items()[i];

                Log::Debug("Check snapshot: '{}'", snapshot.ShadowCopyId());

                auto node = vssNode.AddNode(L"Snapshot {}", snapshot.ShadowCopyId());
                node.AddEmptyLine();

                PrintValue(node, L"Creation", snapshot.CreationTime());
                PrintValue(node, L"Shadow copy set id", snapshot.ShadowCopySetId());
                PrintValue(node, L"Shadow copy id", snapshot.ShadowCopyId());
                PrintValue(node, L"Diff area id", snapshot.Guid());

                PrintValue(
                    node,
                    L"Application info id",
                    ::ToApplicationInfoIdString(snapshot.ApplicationInformation().Guid()));

                PrintValue(node, L"Original snapshots count", snapshot.ApplicationInformation().SnapshotsCount());
                PrintValue(node, L"Machine", snapshot.MachineString());
                PrintValue(node, L"Service", snapshot.ServiceString());

                PrintValue(
                    node,
                    L"Attributes",
                    fmt::format(L"{:#x}", static_cast<uint32_t>(snapshot.VolumeSnapshotAttributes())));

                PrintValue(node, L"Position", snapshot.LayerPosition());
                PrintValue(node, L"Flags", fmt::format("{:#x}", snapshot.Flags()));
                PrintValue(node, L"Context", ToString(snapshot.SnapshotContext()));

                PrintValue(
                    node,
                    L"Size",
                    fmt::format(L"{} ({} bytes)", Traits::ByteQuantity(snapshot.Size()), snapshot.Size()));

                const auto& area = snapshot.DiffAreaInfo();
                PrintValue(node, L"Frn", fmt::format("{:#018x}", area.Frn()));
                PrintValue(node, L"Application info offset", Traits::Offset(area.ApplicationInfoOffset()));
                PrintValue(node, L"Bitmap offset", Traits::Offset(area.FirstBitmapOffset()));
                PrintValue(node, L"Previous bitmap offset", Traits::Offset(area.PreviousBitmapOffset()));
                PrintValue(node, L"Diff area table offset", Traits::Offset(area.FirstDiffAreaTableOffset()));
                PrintValue(node, L"Diff location table offset", Traits::Offset(area.FirstDiffLocationTableOffset()));

                PrintValue(
                    node,
                    L"Allocated size",
                    fmt::format(L"{} ({} bytes)", Traits::ByteQuantity(area.AllocatedSize()), area.AllocatedSize()));

                Snapshot s;
                Snapshot::Parse(*stream, snapshot, s, ec);
                PrintValue(node, L"Parsing", ec ? L"Failed" : L"Ok");
                if (!ec)
                {
                    PrintValue(node, L"Overlay", s.Overlays().size());
                    PrintValue(node, L"Copy on write", s.CopyOnWrites().size());
                    PrintValue(node, L"Forwarder", s.Forwarders().size());
                    PrintValue(node, L"Bitmap size", s.Bitmap().size());
                    PrintValue(node, L"Previous bitmap size", s.PreviousBitmap().size());
                }
                else
                {
                    ec.clear();
                }

                attributes.insert(snapshot.VolumeSnapshotAttributes());

                node.AddEmptyLine();
                node.AddEmptyLine();
            }

            if (attributes.size())
            {
                auto attributeNode = vssNode.AddNode("Attribute values reference:");
                for (const auto attribute : attributes)
                {
                    Print(
                        attributeNode,
                        fmt::format(
                            "{:#x} = {}",
                            static_cast<DWORD>(attribute),
                            FlagsToString(static_cast<DWORD>(attribute), VolumeShadowCopies::g_VssFlags)));
                }

                vssNode.AddEmptyLine();
                vssNode.AddEmptyLine();
            }

            {
                std::vector<Ntfs::ShadowCopy::ShadowCopy> shadowCopies;
                Ntfs::ShadowCopy::ShadowCopy::Parse(*stream, shadowCopies, ec);
                if (ec)
                {
                    Log::Debug("Failed to parse shadow copies");
                    ec.clear();
                }

                for (const auto& shadowCopy : shadowCopies)
                {
                    auto node = vssNode.AddNode("ShadowCopy {}", shadowCopy.Information().ShadowCopyId());
                    node.AddEmptyLine();

                    const auto& info = shadowCopy.Information();
                    PrintValue(node, L"Creation time", info.CreationTime());
                    PrintValue(node, L"Shadow copy set id", info.ShadowCopySetId());
                    PrintValue(node, L"Shadow copy id", info.ShadowCopyId());
                    PrintValue(node, L"Machine", info.MachineString());
                    PrintValue(node, L"Service", info.ServiceString());
                    PrintValue(node, L"Overlay", info.OverlayCount());
                    PrintValue(node, L"Copy on write", info.CopyOnWriteCount());

                    node.AddEmptyLine();

                    if (config.bDump)
                    {
                        Dump(
                            fmt::format(
                                "{}\\vss_shadow_copy_{}.json",
                                ToUtf8(GetWorkingDirectoryApi(ec)),
                                shadowCopy.Information().ShadowCopyId()),
                            shadowCopy);
                    }
                }
            }
        }
    }

    return S_OK;
}

constexpr uint64_t RoundUp(uint64_t offset, uint64_t pageSize)
{
    return (((offset)) + pageSize - 1) & (~(pageSize - 1));
}

HRESULT Orc::Command::NTFSUtil::Main::CommandBitLocker()
{
    using namespace BitLocker;

    auto output = m_console.OutputTree();
    LocationSet aSet;

    // enumerate all possible locations
    HRESULT hr = aSet.EnumerateLocations();
    if (FAILED(hr))
    {
        Log::Critical(L"Failed to enumerate locations [{}]", SystemError(hr));
        return hr;
    }

    if (!config.strVolume.empty())
    {
        std::vector<std::shared_ptr<Location>> addedLocs;
        hr = aSet.AddLocations(config.strVolume.c_str(), addedLocs);
        if (FAILED(hr))
        {
            Log::Critical(L"Failed to add location '{}' [{}]", config.strVolume, SystemError(hr));
            return hr;
        }
    }

    aSet.Consolidate(false, FSVBR::FSType::BITLOCKER);

    auto locationsNode = output.AddNode("BitLocker locations");
    for (const auto& loc : aSet.GetAltitudeLocations())
    {
        auto locationNode = locationsNode.AddNode(loc->GetLocation());

        const auto reader = loc->GetReader();
        reader->Seek(0LLU);

        CBinaryBuffer buffer;
        buffer.SetCount(512);
        ULONGLONG ullBytesRead = 0LLU;

        hr = reader->Read(buffer, 512, ullBytesRead);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to read header from location: '{}'", loc->GetLocation());
        }

        auto pHeader = buffer.GetP<VolumeHeader>();
        if (pHeader->Guid != INFO_GUID)
        {
            Log::Warn("BitLocker header does not have the INFO_GUID");
            continue;
        }

        PrintValue(locationNode, L"SectorSize", Traits::ByteQuantity(pHeader->SectorSize));
        locationNode.AddEmptyLine();

        for (UINT i = 0; i < sizeof(pHeader->InfoOffsets) / sizeof(uint64_t); i++)
        {
            auto metadataNode = locationNode.AddNode("Metadata #{}", i);
            PrintValue(metadataNode, L"Offset", Traits::Offset(pHeader->InfoOffsets[i]));

            reader->Seek(pHeader->InfoOffsets[i]);

            CBinaryBuffer info_buffer;
            info_buffer.SetCount(pHeader->SectorSize);

            ULONGLONG ullBytesRead = 0LLU;
            reader->Read(info_buffer, pHeader->SectorSize, ullBytesRead);

            auto pInfoHeader = info_buffer.GetP<InfoStruct>();

            if (pInfoHeader->Header.Version != 2 && pInfoHeader->Header.Version != 1)
            {
                Log::Warn("Invalid metadata version: '{}', only version 2 is supported", pInfoHeader->Header.Version);
                continue;
            }

            const auto kSignatureSize = 8;
            if (strncmp((char*)pInfoHeader->Header.Signature, "-FVE-FS-", kSignatureSize))
            {
                Log::Warn(
                    "Invalid metadata signature '{}', only -FVE-FS- is supported",
                    ToHexString(
                        std::string_view(reinterpret_cast<char*>(pInfoHeader->Header.Signature), kSignatureSize)));
                continue;
            }

            auto infoSize = (uint64_t)pInfoHeader->Header.Size * (pInfoHeader->Header.Version == 2 ? 16 : 1);

            if (infoSize < 64)
            {
                Log::Warn("Meta data size '{}' is too small", infoSize);
                continue;
            }

            // Reset pointer to start of info data
            reader->Seek(pHeader->InfoOffsets[i]);

            auto toRead = RoundUp(infoSize + sizeof(ValidationHeader), pHeader->SectorSize);
            info_buffer.SetCount(toRead);
            reader->Read(info_buffer, toRead, ullBytesRead);

            auto pValidation = (ValidationHeader*)info_buffer.GetP<BYTE>(infoSize);

            if (pValidation->Version != 1 && pValidation->Version != 2)
            {
                Log::Warn("Invalid validation metadata version {}, only version 2 is supported", pValidation->Version);
                continue;
            }

            const Traits::ByteQuantity size(RoundUp(infoSize + pValidation->Size, pHeader->SectorSize));
            PrintValue(metadataNode, L"Size", size);
        }
    }

    output.AddEmptyLine();
    return S_OK;
}

HRESULT Main::Run()
{
    switch (config.cmd)
    {
        case Main::DetailLocation:
            return CommandLoc();
        case Main::EnumLocations:
            return CommandEnum();
        case Main::MFT:
            return CommandMFT();
        case Main::USN:
            return CommandUSN();
        case Main::Record: {
            return CommandRecord(config.ullRecord);
        }
        case Main::HexDump: {
            return CommandHexDump(config.dwlOffset, config.dwlSize);
        }
        case Main::Vss: {
            return CommandVss();
        }
        case Main::BitLocker: {
            return CommandBitLocker();
        }
    }

    Log::Critical("Unsupported command");
    return E_FAIL;
}
