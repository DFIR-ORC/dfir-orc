//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jeremie Christin
//

#include "stdafx.h"

#include "GetSectors.h"
#include "PartitionTable.h"
#include "TemporaryStream.h"
#include "SystemDetails.h"
#include "FileStream.h"
#include "EnumDisk.h"
#include "Location.h"
#include "PhysicalDiskReader.h"
#include "InterfaceReader.h"
#include "CSVFileWriter.h"
#include <filesystem>
#include <array>

namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::GetSectors;

/*  Notes :
 *  - Un CDiskExtent ne lit jamais si la taille n'est pas un multiple de la taille d'un secteur ?
 *  - tester avec un disque de 4k
 *  - parseur de table des partitions : end sector toujours 0 ?
 *  - ParameterOption : attention à l'ordre des déclarations
 *  - les fichiers avec caractères exotiques ne sont pas ajoutés à une archive ZIP mais le retour de l'utilitaire
 d'archivage indique que tout s'est bien passé
 */

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    std::wstring diskInterfaceToRead;
    if (config.lowInterface)
    {
        // Try to get a low interface on the disk by using the setupAPI functions
        diskInterfaceToRead = identifyLowDiskInterfaceByMbrSignature(config.diskName);
    }
    else
    {
        diskInterfaceToRead = config.diskName;
    }

    if (config.dumpLegacyBootCode || config.dumpUefiFull)
    {
        // Legacy boot code and UEFI predefined logic
        hr = DumpBootCode(config.diskName, diskInterfaceToRead);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to dump boot code of '{}' [{}]", config.diskName, SystemError(hr));
        }
    }

    if (config.dumpSlackSpace)
    {
        // Slack space predefined logic
        hr = DumpSlackSpace(config.diskName, diskInterfaceToRead);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to dump slack space of '{}' [{}]", config.diskName, SystemError(hr));
        }
    }

    if (config.customSample)
    {
        // Specific data requested by the user
        hr = DumpCustomSample(config.diskName, diskInterfaceToRead);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to dump custom sample of '{}' [{}]", config.diskName, SystemError(hr));
        }
    }

    // Produce results
    hr = CollectDiskChunks(config.Output);
    if (FAILED(hr))
    {
        Log::Error(L"Error while writing results for '{}' [{}]", config.diskName, SystemError(hr));
    }

    return hr;
}

std::wstring Main::getBootDiskName()
{
    DWORD ret = 0;
    WCHAR systemDirectory[ORC_MAX_PATH];

    // We assume the windows directory is on the boot disk
    ret = GetWindowsDirectoryW(systemDirectory, ORC_MAX_PATH);
    if (ret == 0 || ret >= ORC_MAX_PATH)
    {
        Log::Error("Failed GetWindowsDirectory [{}]", LastWin32Error());
        return {};
    }

    // Construct volume string in the form \\.\X:
    WCHAR volumeLetter = systemDirectory[0];
    std::wstring volume = L"\\\\.\\";
    volume += volumeLetter;
    volume += L":";

    HANDLE hVolume =
        CreateFileW(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        Log::Error(L"Failed CreateFileW to open volume '{}' [{}]", volume, LastWin32Error());
        return {};
    }

    Guard::Scope sg([&hVolume]() {
        if (hVolume != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hVolume);
            hVolume = INVALID_HANDLE_VALUE;
        }
    });

    VOLUME_DISK_EXTENTS outBuffer;
    DWORD bytesReturned = 0, ioctlLastError = 0;
    BOOL ioctlBool = DeviceIoControl(
        hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &outBuffer, sizeof(outBuffer), &bytesReturned, NULL);
    if (!ioctlBool)
    {
        const auto lastError = GetLastError();
        if (lastError == ERROR_MORE_DATA)
        {
            // The volume span multiple disks.
            // However we are only interested in the first disk and
            // the array "outBuffer.Extents" we provided is of size 1,
            // so we do not need the additionnal data. We do nothing.
            Log::Debug("Failed IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS: ERROR_MORE_DATA");
        }
        else
        {
            Log::Error(L"Failed to retrieve volume disk extents for '{}' [{}]", volume, Win32Error(lastError));
            return {};
        }
    }

    // MSDN :
    // DiskNumber member of the DISK_EXTENT structure is the number of the disk,
    // this is the same number that is used to construct the name of the disk,
    // for example, the X in "\\?\PhysicalDriveX"
    std::wstring diskName = L"\\\\.\\PhysicalDrive";
    diskName += std::to_wstring(outBuffer.Extents[0].DiskNumber);

    m_console.Print(L"Found boot disk: {}", diskName);

    return diskName;
}

/*
 *   Get the disk signature located at offset 0x1B8 in the disk's MBR.
 *   Return value :
 *     - the disk signature as a positive number (unsigned 4-byte value)
 *     - -1 if an error occured
 */
int64_t Main::getDiskSignature(const std::wstring& diskName)
{
    CBinaryBuffer cBuf;
    cBuf.SetCount(MBR_SIZE_IN_BYTES);
    cBuf.ZeroMe();
    DiskChunkStream mbrChunk(diskName, 0, MBR_SIZE_IN_BYTES, L"MBR");
    HRESULT hr = mbrChunk.Read(cBuf.GetData(), MBR_SIZE_IN_BYTES, NULL);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to read disk signature [{}]", SystemError(hr));
        return -1;
    }

    if (cBuf.GetCount() < 0x1BC)
    {
        Log::Error(L"Failed to read disk signature (unexpected size)");
        return -1;
    }

    UINT32 signature = *((UINT32*)(cBuf.GetData() + 0x1B8));
    Log::Debug(L"Signature of the disk '{}': {:#x}", diskName, signature);
    return signature;
}

int64_t Main::getBootDiskSignature()
{
    std::wstring bootDiskName = getBootDiskName();
    if (bootDiskName.empty())
    {
        return -1;
    }

    return getDiskSignature(bootDiskName);
}

std::wstring Main::identifyLowDiskInterfaceByMbrSignature(const std::wstring& diskName)
{
    std::vector<EnumDisk::PhysicalDisk> disksList;
    EnumDisk diskEnum;
    int64_t diskSig = 0;
    std::wstring diskInterface;

    diskSig = getDiskSignature(diskName);
    if (diskSig != -1)
    {
        // Try to get a low device interface on the disk
        diskEnum.EnumerateDisks(disksList);
        for (const auto& disk : disksList)
        {
            if (getDiskSignature(disk.InterfacePath) == diskSig)
            {
                diskInterface = disk.InterfacePath;
                Log::Debug(L"Read operations on '{}' can use the low interface '{}'", diskName, disk.InterfacePath);
                break;
            }
        }
    }

    return diskInterface;
}

HRESULT Main::DiskChunk::read()
{
    HRESULT hr = E_FAIL;
    DWORD numberOfbytesRead = 0, bytesSuccessfullyRead = 0;
    bool getReadingTime = true;
    HRESULT attemptResult = TRUE;
    LARGE_INTEGER offset, perfCountBefore, perfCountAfter;
    offset.QuadPart = 0, perfCountBefore.QuadPart = 0, perfCountAfter.QuadPart = 0;
    ULONGLONG diskSize = 0;
    ULONG diskSectorSize = 0;

    if (m_cBuf.GetData() != NULL)
    {
        // Already read
        return S_OK;
    }

    // Initialize a raw disk reader
    CDiskExtent diskReader(m_DiskInterface);

    hr = diskReader.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to open '{}' for read access [{}]", m_DiskInterface, SystemError(hr));
        return hr;
    }

    diskSize = diskReader.GetLength();
    diskSectorSize = diskReader.GetLogicalSectorSize();

    if (diskSize == 0 || diskSectorSize == 0)
    {
        Log::Error("Unknown disk size or sector size");
        return E_FAIL;
    }

    if (m_ulChunkOffset % diskSectorSize != 0)
    {
        // Round down m_ulChunkOffset to a sector size multiple
        m_ulChunkOffset = m_ulChunkOffset - (m_ulChunkOffset % diskSectorSize);
    }

    if (m_ulChunkSize % diskSectorSize != 0)
    {
        // Round up m_ulChunkSize to a sector size multiple
        m_ulChunkSize = m_ulChunkSize + (diskSectorSize - (m_ulChunkSize % diskSectorSize));
    }

    if (m_ulChunkOffset >= diskSize)
    {
        Log::Error("Offset to read: {} is beyond end of disk: {})", m_ulChunkOffset, diskSize);
        return E_FAIL;
    }

    if (m_ulChunkOffset + m_ulChunkSize > diskSize)
    {
        m_ulChunkSize = (DWORD)(diskSize - m_ulChunkOffset);
    }

    if (m_ulChunkOffset > MAXLONGLONG)
    {
        return hr;
    }

    offset.QuadPart = m_ulChunkOffset;

    hr = diskReader.Seek(offset, NULL, FILE_BEGIN);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to seek on '{}' at offset {} [{}]", m_DiskInterface, m_ulChunkOffset, SystemError(hr));
        return hr;
    }

    if (getReadingTime && !QueryPerformanceCounter(&perfCountBefore))
    {
        Log::Error(
            L"Failed QueryPerformanceCounter before reading at offset {} [{}]", m_ulChunkOffset, LastWin32Error());
        getReadingTime = false;
    }

    m_cBuf.SetCount(m_ulChunkSize);
    m_cBuf.ZeroMe();

    if (m_sectorBySectorMode)
    {
        // Read sector by sector until the desired data size is read or an error occurs when reading a sector.
        while (bytesSuccessfullyRead < m_ulChunkSize && SUCCEEDED(attemptResult))
        {
            attemptResult =
                diskReader.Read(m_cBuf.GetData() + bytesSuccessfullyRead, diskSectorSize, &numberOfbytesRead);
            if (SUCCEEDED(attemptResult))
            {
                bytesSuccessfullyRead += numberOfbytesRead;

                // Manually seek at the next offset to read because in some circumstances,
                // for example when reading the disk using a low interface returned by the setupAPI functions,
                // ReadFile does not increment the internal file pointer for an unknown reason (seen on Windows 10).
                offset.QuadPart = offset.QuadPart + numberOfbytesRead;
                attemptResult = diskReader.Seek(offset, NULL, FILE_BEGIN);
            }
        }
    }
    else
    {
        // Read all data at once. If an error occurs, no data will be available.
        hr = diskReader.Read(m_cBuf.GetData(), m_ulChunkSize, &numberOfbytesRead);
        if (FAILED(hr))
        {
            // Free the memory for the buffer, m_cBuf.GetData() will return NULL
            m_cBuf.RemoveAll();

            Log::Error(L"Failed on '{}' to read at offset {}", m_DiskInterface, m_ulChunkOffset);
            return hr;
        }

        bytesSuccessfullyRead = numberOfbytesRead;
    }

    if (getReadingTime && !QueryPerformanceCounter(&perfCountAfter))
    {
        Log::Error(
            L"Failed QueryPerformanceCounter before reading at offset {} [{}]", m_ulChunkOffset, LastWin32Error());
        getReadingTime = false;
    }

    if (getReadingTime)
    {
        m_readingTime = perfCountAfter.QuadPart - perfCountBefore.QuadPart;
    }

    if (bytesSuccessfullyRead != m_ulChunkSize)
    {
        Log::Warn(
            L"Failed on '{}' to read all of the {} bytes at offset {}, only {} bytes have been read",
            m_DiskInterface,
            m_ulChunkSize,
            m_ulChunkOffset,
            bytesSuccessfullyRead);

        m_ulChunkSize = bytesSuccessfullyRead;
        m_cBuf.SetCount(m_ulChunkSize);
    }

    return S_OK;
}

std::wstring Main::DiskChunk::getSampleName()
{
    std::wstring sanitizedDiskName(m_DiskName);
    std::replace(sanitizedDiskName.begin(), sanitizedDiskName.end(), '\\', '_');
    std::wstring tmpDescription(m_description);
    std::replace(tmpDescription.begin(), tmpDescription.end(), ' ', '-');
    return sanitizedDiskName + L"_off_" + std::to_wstring(m_ulChunkOffset) + L"_len_" + std::to_wstring(m_ulChunkSize)
        + L"_" + tmpDescription + L".bin";
}

bool Main::extractInfoFromLocation(
    const std::shared_ptr<Location>& location,
    std::wstring& out_deviceName,
    ULONGLONG& out_offset,
    ULONGLONG& out_size)
{
    auto physDiskReader = std::dynamic_pointer_cast<PhysicalDiskReader>(location->GetReader());
    if (physDiskReader != NULL)
    {
        out_offset = physDiskReader->GetOffset();
        out_size = physDiskReader->GetLength();
        out_deviceName = physDiskReader->GetPath();
        return true;
    }

    auto interfaceReader = std::dynamic_pointer_cast<InterfaceReader>(location->GetReader());
    if (interfaceReader != NULL)
    {
        out_offset = interfaceReader->GetOffset();
        out_size = interfaceReader->GetLength();
        out_deviceName = interfaceReader->GetPath();
        return true;
    }

    return false;
}

std::shared_ptr<DiskChunkStream>
Main::readAtVolumeLevel(const std::wstring& diskName, ULONGLONG offset, DWORD size, const std::wstring& description)
{
    LocationSet aSet;

    Log::Debug(L"readAtVolumeLevel: device '{}' at offset: {}", diskName, offset);
    if (auto hr = aSet.EnumerateLocations(); FAILED(hr))
    {
        Log::Error(L"Failed to enumerate locations [{}]", SystemError(hr));
        return nullptr;
    }

    aSet.Consolidate(false, FSVBR::FSType::ALL);

    std::wstring volDeviceName;
    ULONGLONG volOffset;
    ULONGLONG volSize;

    for (const auto& volume : aSet.GetVolumes())
    {
        ULONGLONG serialNumber = volume.first;
        LocationSet::VolumeLocations volumeLocations = volume.second;
        bool found = false;

        // TODO fabienfl: I do not trust this loop, found is set for a location immediately changed
        for (const auto& location : volumeLocations.Locations)
        {
            Log::Debug(L"Accessing location: '{}', type: {}", location, location->GetType());

            if (found)
            {
                // Higher level allows to bypass disk encryption
                Log::Info(L"Higher level access for '{}'", location->GetLocation());
                if (location->GetType() == Location::Type::MountedVolume)
                {
                    if (offset < volOffset || offset >= volOffset + volSize)
                    {
                        Log::Error(L"Failed to use high level interface, offset is outside volume boundary");
                        found = false;
                        continue;
                    }

                    return std::make_shared<DiskChunkStream>(
                        diskName, offset - volOffset, size, description, location->GetLocation());
                }
            }
            else if (extractInfoFromLocation(location, volDeviceName, volOffset, volSize))
            {
                if ((_wcsicmp(diskName.c_str(), volDeviceName.c_str()) == 0) && (offset > volOffset)
                    && (offset < (volOffset + volSize)))
                {
                    // The data we want to read is located in this volume
                    Log::Error(
                        L"Found '{}' by using altitude selector. Now trying to get a higher level access",
                        location->GetLocation());
                    found = true;
                }
            }
        }
    }

    Log::Error(L"Failed to get a higher access level");
    return nullptr;
}

/*
 *	Method to dump legacy boot code (MBR, VBR, IPL) or GPT and UEFI related data.
 */
HRESULT Main::DumpBootCode(const std::wstring& diskName, const std::wstring& diskInterfaceToRead)
{
    HRESULT hr = E_FAIL;
    DWORD numberOfbytesRead = 0, retval = 0;

    // List partitions
    PartitionTable partTable;
    hr = partTable.LoadPartitionTable(diskName.c_str());
    if (FAILED(hr))
    {
        Log::Error("Failed to load partition table [{}]", SystemError(hr));
        return hr;
    }

    if (partTable.Table().empty())
    {
        Log::Warn("Partition table is empty");
        return hr;
    }

    if (partTable.isGPT())
    {
        /*
         *   We dump the GPT which should be composed of two parts :
         *	- the GPT header
         *	- an array of GPT partition entries
         */
        DumpRawGPT(diskName, diskInterfaceToRead);
    }

    if (config.dumpUefiFull)
    {
        DWORD efiPartitionSizeToDump = 0;
        for (const auto& partition : partTable.Table())
        {
            if (partition.IsESP())  // EFI system partition
            {
                if (partition.Size > config.uefiFullMaxSize)  // Truncate the dump if the partition is too large
                {
                    efiPartitionSizeToDump = (DWORD)config.uefiFullMaxSize;
                    Log::Warn(
                        L"UEFI partition too large, the dump will be truncated to {} bytes (partition start offset "
                        L"{}, real size {} bytes).",
                        efiPartitionSizeToDump,
                        partition.Start,
                        partition.Size);
                }
                else
                {
                    efiPartitionSizeToDump = (DWORD)partition.Size;
                }

                dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                    diskName, partition.Start, efiPartitionSizeToDump, L"EFI-partition", diskInterfaceToRead));
            }
        }
    }

    if (!config.dumpLegacyBootCode)
    {
        return S_OK;
    }

    /*
     *  Dump code executed at startup between a legacy BIOS (non-UEFI) and Windows,
     *  i.e. MBR, VBR and Initial Program Loader (IPL)
     */

    /*
     *  Dump MBR
     */
    dumpedChunks.push_back(
        std::make_shared<DiskChunkStream>(diskName, 0, MBR_SIZE_IN_BYTES, L"MBR", diskInterfaceToRead));

    std::shared_ptr<DiskChunkStream> vbrChunk, vbrBackupChunk;
    DWORD64 hiddenSectors = 0, hiddenSectorsVbrBackup = 0;
    std::shared_ptr<DiskChunkStream> iplChunk, iplChunk2;
    UINT iplSizeInBytes = 0;
    ULONGLONG iplOffset = 0;
    std::wstring description, partitionDesc;

    // Iterate over partitions
    for (const auto& partition : partTable.Table())
    {
        m_console.Print(
            "Partition number: {}, type: {}, flags: {}, sector size: {}, start: {}, end: {}, size: {}",
            partition.PartitionNumber,
            partition.PartitionType,
            partition.PartitionFlags,
            partition.SectorSize,
            partition.Start,
            partition.End,
            partition.Size);

        if (partition.IsNTFS() || partition.IsBitLocked())
        {
            partitionDesc = L"partition " + std::to_wstring(partition.PartitionNumber);
            if (partition.IsBootable())
            {
                partitionDesc += L" (bootable flag)";
            }

            /*
             *  Dump VBR
             */
            description = L"VBR of " + partitionDesc;
            vbrChunk = std::make_shared<DiskChunkStream>(
                diskName, partition.Start, VBR_SIZE_IN_BYTES, description.c_str(), diskInterfaceToRead);
            dumpedChunks.push_back(vbrChunk);

            /*
             *  Dump potential VBR backup
             */
            description = L"VBR backup of " + partitionDesc;

            if (partTable.isGPT())
            {
                // VBR should be located just after the end of the NTFS partition
                vbrBackupChunk = std::make_shared<DiskChunkStream>(
                    diskName,
                    partition.Start + partition.Size,
                    VBR_SIZE_IN_BYTES,
                    description.c_str(),
                    diskInterfaceToRead);
            }
            else  // Non GPT
            {
                // VBR should be located at the end of the NTFS partition
                vbrBackupChunk = std::make_shared<DiskChunkStream>(
                    diskName,
                    partition.Start + partition.Size - VBR_SIZE_IN_BYTES,
                    VBR_SIZE_IN_BYTES,
                    description.c_str(),
                    diskInterfaceToRead);

                // We read it now to verify if it's a valid VBR backup.
                CBinaryBuffer vbrBackupCheck;
                ULONGLONG bytesRead = 0;
                vbrBackupCheck.SetCount((size_t)vbrBackupChunk->GetSize());
                if (vbrBackupChunk->Read(vbrBackupCheck.GetData(), vbrBackupChunk->GetSize(), &bytesRead) == S_OK)
                {
                    BYTE* tmpPtr = vbrBackupCheck.GetData();
                    if (bytesRead >= 0x200 && tmpPtr[0x1fe] == 0x55 && tmpPtr[0x1ff] == 0xAA)
                    {
                        // We have a valid VBR backup, do nothing else.
                    }
                    else
                    {
                        // We don't have a valid VBR backup.
                        // In case this is due to an encryption layer, like Bitlocker, we try to read at a higher level
                        auto clearVbrBackup = readAtVolumeLevel(
                            diskName,
                            partition.Start + partition.Size - VBR_SIZE_IN_BYTES,
                            VBR_SIZE_IN_BYTES,
                            description);
                        if (clearVbrBackup)
                        {
                            vbrBackupChunk = clearVbrBackup;
                        }
                    }
                }

                // We cancel the effect of our potential reading by resetting the pointer of the chunk
                vbrBackupChunk->SetFilePointer(0, FILE_BEGIN, NULL);
            }
            dumpedChunks.push_back(vbrBackupChunk);

            /*
             *  Dump Initial Program Loader (IPL)
             */
            CBinaryBuffer vbr_cBuf;
            vbr_cBuf.SetCount((size_t)vbrChunk->GetSize());
            HRESULT readRetVal = vbrChunk->Read(vbr_cBuf.GetData(), vbr_cBuf.GetCount(), NULL);

            {
                auto vbr = reinterpret_cast<PackedBootSector*>(vbr_cBuf.GetData());
                if (vbr == NULL || readRetVal != S_OK)
                {
                    Log::Debug(L"Cannot parse VBR of partition number {} on '{}'", partition.PartitionNumber, diskName);
                }
                else if (reinterpret_cast<BYTE*>(vbr)[0x1FE] == 0x55 && reinterpret_cast<BYTE*>(vbr)[0x1FF] == 0xAA)
                {
                    hiddenSectors = vbr->PackedBpb.HiddenSectors;
                    m_console.Print(
                        L"VBR hidden sector value for partition number {}: {} on '{}'",
                        partition.PartitionNumber,
                        hiddenSectors,
                        diskName);

                    iplSizeInBytes = IPL_SIZE_IN_SECTORS * partition.SectorSize;
                    iplOffset = hiddenSectors * partition.SectorSize + partition.SectorSize;

                    description = L"IPL of " + partitionDesc;
                    dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                        diskName, iplOffset, iplSizeInBytes, description.c_str(), diskInterfaceToRead));

                    if (iplOffset != (partition.Start + partition.SectorSize))
                    {
                        description = L"Data at offset where IPL is usually located for " + partitionDesc;
                        auto dataAtUsualIplPlace = std::make_shared<DiskChunkStream>(
                            diskName,
                            partition.Start + partition.SectorSize,
                            iplSizeInBytes,
                            description.c_str(),
                            diskInterfaceToRead);
                        dumpedChunks.push_back(dataAtUsualIplPlace);
                    }
                }
                else
                {
                    Log::Debug(L"Invalid VBR for partition number {} on '{}'", partition.PartitionNumber, diskName);
                }
            }

            /*
             *  Dump Initial Program Loader (IPL) by using hiddenSectors value from VBR backup
             */
            vbr_cBuf.ZeroMe();
            vbr_cBuf.SetCount((size_t)vbrBackupChunk->GetSize());
            readRetVal = vbrBackupChunk->Read(vbr_cBuf.GetData(), vbr_cBuf.GetCount(), NULL);

            {
                auto vbr = reinterpret_cast<PackedBootSector*>(vbr_cBuf.GetData());
                if (vbr == NULL || readRetVal != S_OK)
                {
                    Log::Debug(
                        L"Cannot parse VBR backup of partition number {} '{}'", partition.PartitionNumber, diskName);
                }
                else if (reinterpret_cast<BYTE*>(vbr)[0x1FE] == 0x55 && reinterpret_cast<BYTE*>(vbr)[0x1FF] == 0xAA)
                {
                    hiddenSectorsVbrBackup = vbr->PackedBpb.HiddenSectors;
                    m_console.Print(
                        L"VBR backup hidden sector value for partition number {}: {:#x}) on '{}'",
                        partition.PartitionNumber,
                        hiddenSectorsVbrBackup,
                        diskName);

                    // Only dump IPL at this offset if it is different from the one found in normal VBR
                    if (hiddenSectorsVbrBackup != hiddenSectors)
                    {
                        iplSizeInBytes = IPL_SIZE_IN_SECTORS * partition.SectorSize;
                        iplOffset = hiddenSectorsVbrBackup * partition.SectorSize + partition.SectorSize;

                        description = L"IPL of " + partitionDesc + L" by following VBR backup";
                        dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                            diskName, iplOffset, iplSizeInBytes, description.c_str(), diskInterfaceToRead));
                    }
                }
                else
                {
                    Log::Debug(
                        L"Invalid VBR backup for partition number {} on '{}'", partition.PartitionNumber, diskName);
                }
            }
        }
    }

    return S_OK;
}

HRESULT Main::DumpRawGPT(const std::wstring& diskName, const std::wstring& diskInterfaceToRead)
{
    CDiskExtent diskExtent(diskInterfaceToRead);
    LARGE_INTEGER offsetToRead;
    ULONGLONG dwBytesRead = 0;
    ULONG sectorSize;

    HRESULT hr = diskExtent.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
    if (FAILED(hr))
    {
        Log::Error(L"Could not open Location '{}' [{}]", diskExtent.GetName(), SystemError(hr));
        return hr;
    }

    // Warning : when the disk is a regular file (eg. image of a disk acquired with dd), the sector size of the original
    // disk may be wrong.
    sectorSize = diskExtent.GetLogicalSectorSize();

    diskExtent.Close();

    /*
    // Get the primary GPT always located at LBA 1
    */
    offsetToRead.QuadPart = sectorSize;

    // Sanity check
    if (sectorSize < sizeof(GPTHeader))
    {
        return hr;
    }

    auto gptHeaderPrimary = std::make_shared<DiskChunkStream>(
        diskName, offsetToRead.QuadPart, sectorSize, L"GPT-primary-header", diskInterfaceToRead);
    dumpedChunks.push_back(gptHeaderPrimary);

    CBinaryBuffer buffer;
    buffer.SetCount(sectorSize);

    if (SUCCEEDED(gptHeaderPrimary->Read(buffer.GetData(), buffer.GetCount(), &dwBytesRead)))
    {
        gptHeaderPrimary->SetFilePointer(0, FILE_BEGIN, NULL);
        PGPTHeader gptHeader = reinterpret_cast<PGPTHeader>(buffer.GetData());

        // check GPT signature
        if (std::strncmp(reinterpret_cast<char*>(gptHeader->Signature), "EFI PART", 8))
        {
            Log::Error(L"Bad GPT signature");
            return hr;
        }

        // check validity of some parameters
        if (gptHeader->NumberOfPartitionEntries > 128)
        {
            Log::Error("Abnormally large number of GPT partition entries: {}", gptHeader->NumberOfPartitionEntries);
            return hr;
        }

        if (gptHeader->SizeofPartitionEntry > 0x10000)
        {
            Log::Error("Abnormally large partition entry: {}", gptHeader->SizeofPartitionEntry);
            return hr;
        }

        // Dump partition table entries
        dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
            diskName,
            gptHeader->PartitionEntryLba * sectorSize,
            gptHeader->NumberOfPartitionEntries * gptHeader->SizeofPartitionEntry,
            L"GPT-primary-partition-entries",
            diskInterfaceToRead));

        /*
        // Get the secondary GPT
        */
        auto gptHeaderSecondary = std::make_shared<DiskChunkStream>(
            diskName, gptHeader->AlternativeLba * sectorSize, sectorSize, L"GPT-secondary-header", diskInterfaceToRead);
        dumpedChunks.push_back(gptHeaderSecondary);

        buffer.ZeroMe();
        buffer.SetCount(sectorSize);
        if (SUCCEEDED(gptHeaderSecondary->Read(buffer.GetData(), buffer.GetCount(), &dwBytesRead)))
        {
            gptHeaderSecondary->SetFilePointer(0, FILE_BEGIN, NULL);
            PGPTHeader gptHeader = reinterpret_cast<PGPTHeader>(buffer.GetData());
            // check GPT signature
            if (std::strncmp(reinterpret_cast<char*>(gptHeader->Signature), "EFI PART", 8))
            {
                Log::Error(L"Bad GPT signature (secondary GPT)");
                return hr;
            }

            // check validity of some parameters
            if (gptHeader->NumberOfPartitionEntries > 128)
            {
                Log::Error(
                    L"Abnormally large number of GPT partition entries: {} (secondary GPT)",
                    gptHeader->NumberOfPartitionEntries);
                return hr;
            }

            if (gptHeader->SizeofPartitionEntry > 0x10000)
            {
                Log::Error(L"Abnormally large partition entry: {} (secondary GPT)", gptHeader->SizeofPartitionEntry);
                return hr;
            }

            // Dump partition table entries
            dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                diskName,
                gptHeader->PartitionEntryLba * sectorSize,
                gptHeader->NumberOfPartitionEntries * gptHeader->SizeofPartitionEntry,
                L"GPT-secondary-partition-entries",
                diskInterfaceToRead));
        }
    }

    return S_OK;
}

bool partitionSortByStartAddress(std::map<std::string, ULONGLONG>& item1, std::map<std::string, ULONGLONG>& item2)
{
    return item1["start"] < item2["start"];
}

HRESULT Main::DumpSlackSpace(const std::wstring& diskName, const std::wstring& diskInterfaceToRead)
{
    HRESULT hr = E_FAIL;
    PVOID pBuf = NULL;
    PartitionTable pt;

    std::vector<std::map<std::string, ULONGLONG>> usedLocations;
    std::map<std::string, ULONGLONG> loc;
    ULONGLONG ulSlackSpaceOffset = 0;
    DWORD slackSpaceSize = 0;

    hr = pt.LoadPartitionTable(diskName.c_str());
    if (FAILED(hr))
    {
        Log::Error("Failed to load partition table [{}]", SystemError(hr));
        return hr;
    }

    if (pt.Table().empty())
    {
        Log::Warn("Partition Table empty");
        return 0;
    }

    // Add locations we already dump by using other heuristics (for example MBR or GPT while dumping legacy boot code)
    for (const auto& chunk : dumpedChunks)
    {
        loc["start"] = chunk->m_offset,
        loc["end"] = chunk->m_offset + chunk->m_size;  // first is an offset, second is a size
        usedLocations.push_back(loc);
    }

    // Add locations used by partitions
    std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
        loc["start"] = part.Start, loc["end"] = part.Start + part.Size;
        if (!part.IsExtented())
        {
            usedLocations.push_back(loc);
        }
    });

    // Get the disk size to add a dummy end of disk location
    CDiskExtent diskReader(diskInterfaceToRead);
    ULONGLONG diskSize = 0;
    if (FAILED(hr = diskReader.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING)))
    {
        Log::Error(L"Failed to open a disk for read access '{}' [{}]", diskInterfaceToRead, SystemError(hr));
        return hr;
    }

    diskReader.Close();
    diskSize = diskReader.GetLength();

    // Add a dummy end of disk location
    loc["start"] = diskSize, loc["end"] = diskSize;
    usedLocations.push_back(loc);

    std::sort(usedLocations.begin(), usedLocations.end(), partitionSortByStartAddress);

    // This variable will be updated to store the farthest end offset seen.
    // This is used to correctly handle potential overlapping locations
    ULONGLONG farthestEnd = 0;

    for (auto& curLoc : usedLocations)
    {
        if (farthestEnd < curLoc["start"])
        {
            ulSlackSpaceOffset = farthestEnd;
            slackSpaceSize = (DWORD)(curLoc["start"] - farthestEnd);
            if (slackSpaceSize > config.slackSpaceDumpSize)
            {
                slackSpaceSize = (DWORD)config.slackSpaceDumpSize;
            }

            auto diskChunk = std::make_shared<DiskChunkStream>(
                diskName, ulSlackSpaceOffset, slackSpaceSize, L"Disk slack space", diskInterfaceToRead);
            diskChunk->setSectorBySectorMode(true);
            dumpedChunks.push_back(diskChunk);
        }

        if (curLoc["end"] > farthestEnd)
        {
            farthestEnd = curLoc["end"];
        }
    }

    return S_OK;
}

HRESULT Main::DumpCustomSample(const std::wstring& diskName, const std::wstring& diskInterfaceToRead)
{
    dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
        diskName, config.customSampleOffset, config.customSampleSize, L"Custom sample", diskInterfaceToRead));
    return S_OK;
}

HRESULT Main::CollectDiskChunks(const OutputSpec& output)
{
    HRESULT hr = E_FAIL;

    switch (output.Type)
    {
        case OutputSpec::Kind::Archive: {
            auto compressor = ArchiveCreate::MakeCreate(config.Output.ArchiveFormat, true);

            auto [hr, CSV] = CreateDiskChunkArchiveAndCSV(config.Output.Path, compressor);

            if (FAILED(hr))
                return hr;

            for (auto& diskChunk : dumpedChunks)
            {
                // Make sure we read from the beginning of the chunk
                diskChunk->SetFilePointer(0, FILE_BEGIN, NULL);

                hr = CollectDiskChunk(compressor, *CSV, diskChunk);
                if (FAILED(hr))
                {
                    Log::Error(L"Unable to archive result for '{}' [{}]", diskChunk->m_description, SystemError(hr));
                }
            }

            FinalizeArchive(compressor, CSV);
        }
        break;
        case OutputSpec::Kind::Directory: {
            auto [hr, CSV] = CreateOutputDirLogFileAndCSV(config.Output.Path);

            if (FAILED(hr))
                return hr;

            for (auto& diskChunk : dumpedChunks)
            {
                // Make sure we read from the beginning of the chunk
                diskChunk->SetFilePointer(0, FILE_BEGIN, NULL);

                hr = CollectDiskChunk(config.Output.Path, *CSV, diskChunk);
                if (FAILED(hr))
                {
                    Log::Error(L"Unable to archive result for '{}' [{}]", diskChunk->m_description, SystemError(hr));
                }
            }

            CSV->Close();
        }
        break;
        default:
            return E_NOTIMPL;
    }

    return S_OK;
}

std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
Main::CreateDiskChunkArchiveAndCSV(const std::wstring& pArchivePath, const std::shared_ptr<ArchiveCreate>& compressor)
{
    HRESULT hr = E_FAIL;

    if (pArchivePath.empty())
        return {E_POINTER, nullptr};

    fs::path tempdir;
    tempdir = fs::path(pArchivePath).parent_path();

    auto csvStream = std::make_shared<TemporaryStream>();

    if (FAILED(hr = csvStream->Open(tempdir.wstring(), L"GetSectors", 1 * 1024 * 1024)))
    {
        Log::Error(L"Failed to create temp stream [{}]", SystemError(hr));
        return {hr, nullptr};
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;

    auto CSV = TableOutput::CSV::Writer::MakeNew(std::move(options));

    if (FAILED(hr = CSV->WriteToStream(csvStream)))
    {
        Log::Error(L"Failed to initialize CSV stream [{}]", SystemError(hr));
        return {hr, nullptr};
    }
    CSV->SetSchema(config.Output.Schema);

    if (FAILED(hr = compressor->InitArchive(pArchivePath.c_str())))
    {
        Log::Error(L"Failed to initialize archive file '{}' [{}]", pArchivePath, SystemError(hr));
        return {hr, nullptr};
    }

    compressor->SetCallback(
        [this](const OrcArchive::ArchiveItem& item) { Log::Info(L"File archived: '{}'", item.NameInArchive); });

    return {S_OK, std::move(CSV)};
}

std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
Main::CreateOutputDirLogFileAndCSV(const std::wstring& strOutputDir)
{
    HRESULT hr = E_FAIL;

    if (strOutputDir.empty())
    {
        return {E_POINTER, nullptr};
    }

    fs::path outDir(strOutputDir);

    switch (fs::status(outDir).type())
    {
        case fs::file_type::not_found:
            if (create_directories(outDir))
            {
                Log::Debug(L"Created output directory '{}'", strOutputDir);
            }
            else
            {
                Log::Debug(L"Output directory '{}' already exists", strOutputDir);
            }
            break;
        case fs::file_type::directory:
            Log::Debug(L"Specified output directory '{}' exists and is a directory", strOutputDir);
            break;
        default:
            Log::Error(L"Specified output directory '{}' exists and is not a directory", strOutputDir);
            break;
    }

    fs::path csvFile(outDir);
    csvFile /= L"GetSectors.csv";

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;

    auto CSV = TableOutput::CSV::Writer::MakeNew(std::move(options));

    hr = CSV->WriteToFile(csvFile.wstring().c_str());
    if (FAILED(hr))
    {
        return {hr, nullptr};
    }

    CSV->SetSchema(config.Output.Schema);

    return {S_OK, std::move(CSV)};
}

HRESULT Main::CollectDiskChunk(
    const std::shared_ptr<ArchiveCreate>& compressor,
    ITableOutput& output,
    std::shared_ptr<DiskChunkStream> diskChunk)
{
    HRESULT hr = compressor->AddStream(diskChunk->getSampleName().c_str(), L"dummy", diskChunk);
    if (FAILED(hr))
    {
        // TODO: fabienfl: should return on failure ?
        // note : pas d'erreur lorsqu'un sample avec un nom bizarre n'a pas été mis dans dans un zip
        Log::Error(L"Failed to add a sample to the archive [{}]", SystemError(hr));
    }

    std::wstring computerName;
    SystemDetails::GetOrcComputerName(computerName);
    hr = AddDiskChunkRefToCSV(output, computerName, *diskChunk);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to add a sample metadata to csv [{}]", SystemError(hr));
    }

    hr = compressor->FlushQueue();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to flush queue to '{}' [{}]", config.Output.Path, SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::FinalizeArchive(
    const std::shared_ptr<ArchiveCreate>& compressor,
    const std::shared_ptr<TableOutput::IWriter>& CSV)
{
    HRESULT hr = E_FAIL;

    CSV->Flush();

    // Archive CSV
    auto pStreamWriter = std::dynamic_pointer_cast<TableOutput::IStreamWriter>(CSV);
    if (pStreamWriter && pStreamWriter->GetStream())
    {
        auto pCSVStream = pStreamWriter->GetStream();
        if (pCSVStream && pCSVStream->GetSize() > 0LL)
        {
            hr = pCSVStream->SetFilePointer(0, FILE_BEGIN, nullptr);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to rewind csv stream [{}]", SystemError(hr));
            }

            hr = compressor->AddStream(L"GetSectors.csv", L"GetSectors.csv", pCSVStream);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to add GetSectors.csv [{}]", SystemError(hr));
            }
        }
    }

    hr = compressor->Complete();
    if (FAILED(hr))
    {
        Log::Error(L"Failed to complete '{}' [{}]", config.Output.Path, SystemError(hr));
        return hr;
    }

    CSV->Close();
    return S_OK;
}

HRESULT
Main::CollectDiskChunk(const std::wstring& outputdir, ITableOutput& output, std::shared_ptr<DiskChunkStream> diskChunk)
{
    // TODO : read by chunks
    CBinaryBuffer cBuf;
    cBuf.SetCount((size_t)diskChunk->GetSize());
    cBuf.ZeroMe();

    ULONGLONG ullBytesRead = 0LL;
    HRESULT hr = diskChunk->Read(cBuf.GetData(), cBuf.GetCount(), &ullBytesRead);
    if (hr != S_OK)
    {
        return hr;
    }

    const fs::path outputDir(outputdir);
    const fs::path sampleFile = outputDir / fs::path(diskChunk->getSampleName());
    FileStream outputStream;
    hr = outputStream.WriteTo(sampleFile.wstring().c_str());
    if (FAILED(hr))
    {
        Log::Error(L"Failed to create sample file '{}' [{}]", sampleFile, SystemError(hr));
        return hr;
    }

    ULONGLONG ullBytesWritten = 0LL;
    hr = outputStream.Write(cBuf.GetData(), cBuf.GetCount(), &ullBytesWritten);
    if (FAILED(hr))
    {
        Log::Error(L"Failed while writing to sample '{}' [{}]", sampleFile, SystemError(hr));
        return hr;
    }

    outputStream.Close();

    std::wstring computerName;
    SystemDetails::GetOrcComputerName(computerName);
    hr = AddDiskChunkRefToCSV(output, computerName, *diskChunk);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to add diskChunk metadata to csv [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT
Main::AddDiskChunkRefToCSV(ITableOutput& output, const std::wstring& strComputerName, DiskChunkStream& diskChunk)
{
    output.WriteString(strComputerName);
    output.WriteString(diskChunk.m_DiskName);
    output.WriteString(diskChunk.m_description);
    output.WriteString(diskChunk.getSampleName());
    output.WriteInteger(diskChunk.m_offset);
    output.WriteInteger(diskChunk.m_size);
    output.WriteInteger(diskChunk.m_readingTime);
    output.WriteString(diskChunk.m_DiskInterface);
    output.WriteInteger(diskChunk.m_diskReader->GetLogicalSectorSize());
    output.WriteEndOfLine();

    return S_OK;
}
