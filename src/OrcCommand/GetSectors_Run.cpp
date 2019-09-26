//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jeremie Christin
//

#include "stdafx.h"

#include "LogFileWriter.h"
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

using namespace std;

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

    TODO :

 */

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    wstring diskInterfaceToRead = L"";
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
        if (FAILED(DumpBootCode(config.diskName, diskInterfaceToRead)))
        {
            log::Error(_L_, hr, L"[%s] Error while dumping legacy or UEFI boot code\r\n", config.diskName.c_str());
        }
    }

    if (config.dumpSlackSpace)
    {
        // Slack space predefined logic
        if (FAILED(DumpSlackSpace(config.diskName, diskInterfaceToRead)))
        {
            log::Error(_L_, hr, L"[%s] Error while dumping slack space\r\n", config.diskName.c_str());
        }
    }

    if (config.customSample)
    {
        // Specific data requested by the user
        if (FAILED(DumpCustomSample(config.diskName, diskInterfaceToRead)))
        {
            log::Error(_L_, hr, L"[%s] Error while dumping custom sample\r\n", config.diskName.c_str());
        }
    }

    // Produce results
    if (FAILED(CollectDiskChunks(config.Output)))
    {
        log::Error(_L_, hr, L"Error while writing results\r\n");
    }
    else
    {
        hr = S_OK;
    }

    return hr;
}

wstring Main::getBootDiskName()
{
    DWORD ret = 0;
    WCHAR systemDirectory[MAX_PATH];

    // We assume the windows directory is on the boot disk
    ret = GetWindowsDirectoryW(systemDirectory, MAX_PATH);
    if (ret == 0 || ret >= MAX_PATH)
    {
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to retrieve windows directory\r\n");
        return L"";
    }

    // Construct volume string in the form \\.\X:
    WCHAR volumeLetter = systemDirectory[0];
    wstring volume = L"\\\\.\\";
    volume += volumeLetter;
    volume += L":";

    HANDLE hVolume = 0;
    // Open volume
    hVolume =
        CreateFileW(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to open volume %s\r\n", volume.c_str());
        return L"";
    }

    BOOST_SCOPE_EXIT(&hVolume)
    {
        if (hVolume != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hVolume);
            hVolume = INVALID_HANDLE_VALUE;
        }
    }
    BOOST_SCOPE_EXIT_END;

    BOOL ioctlBool;
    VOLUME_DISK_EXTENTS outBuffer;
    DWORD bytesReturned = 0, ioctlLastError = 0;
    ioctlBool = DeviceIoControl(
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

            log::Info(
                _L_, L"[getBootDiskName] IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned error ERROR_MORE_DATA\r\n");
        }
        else
        {
            log::Error(
                _L_,
                HRESULT_FROM_WIN32(lastError),
                L"Failed to retrive volume disk extents for %s\r\n",
                volume.c_str());
            return L"";
        }
    }

    // MSDN :
    // DiskNumber member of the DISK_EXTENT structure is the number of the disk,
    // this is the same number that is used to construct the name of the disk,
    // for example, the X in "\\?\PhysicalDriveX"
    wstring diskName = L"\\\\.\\PhysicalDrive";
    diskName += std::to_wstring(outBuffer.Extents[0].DiskNumber);

    // log::Info(_L_, L"Deduced boot disk : %s\r\n", diskName.c_str());

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
    DiskChunkStream mbrChunk(_L_, diskName, 0, MBR_SIZE_IN_BYTES, L"MBR");
    if (FAILED(mbrChunk.Read(cBuf.GetData(), MBR_SIZE_IN_BYTES, NULL)))
    {
        return -1;
    }

    if (cBuf.GetCount() < 0x1BC)
    {
        return -1;
    }

    UINT32 signature = *((UINT32*)(cBuf.GetData() + 0x1B8));
    // log::Info(_L_, L"Signature of the disk \"%s\" : 0x%08X\r\n", diskName.c_str(), signature);
    return signature;
}

int64_t Main::getBootDiskSignature()
{
    wstring bootDiskName = getBootDiskName();
    if (bootDiskName.empty())
    {
        return -1;
    }

    return getDiskSignature(bootDiskName);
}

std::wstring Main::identifyLowDiskInterfaceByMbrSignature(const std::wstring& diskName)
{
    std::vector<EnumDisk::PhysicalDisk> disksList;
    EnumDisk diskEnum(_L_);
    int64_t diskSig = 0;
    std::wstring diskInterface = L"";

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
                log::Info(
                    _L_,
                    L"[%s] Read operations can use the low interface \"%s\"\r\n",
                    diskName.c_str(),
                    disk.InterfacePath.c_str());
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
    CDiskExtent diskReader(_L_, m_DiskInterface);
    if (FAILED(hr = diskReader.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING)))
    {
        log::Error(_L_, hr, L"Failed to open a disk for read access (%s)\n", m_DiskInterface.c_str());
        return hr;
    }

    diskSize = diskReader.GetLength();
    diskSectorSize = diskReader.GetLogicalSectorSize();

    if (diskSize == 0 || diskSectorSize == 0)
    {
        log::Error(_L_, E_FAIL, L"Unknown disk size or sector size\r\n");
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
        log::Error(
            _L_,
            E_FAIL,
            L"[GetSectorsCmd] Offset to read is beyond end of disk. (offset = %llu, disk size = %llu)\r\n",
            m_ulChunkOffset,
            diskSize);
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

    if (FAILED(hr = diskReader.Seek(offset, NULL, FILE_BEGIN)))
    {
        log::Error(_L_, hr, L"Failed to seek at offset %llu (%s)\n", m_ulChunkOffset, m_DiskInterface.c_str());
        return hr;
    }

    if (getReadingTime && !QueryPerformanceCounter(&perfCountBefore))
    {
        log::Info(_L_, L"QueryPerformanceCounter before reading at offset %llu failed", m_ulChunkOffset);
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
        if (FAILED(hr = diskReader.Read(m_cBuf.GetData(), m_ulChunkSize, &numberOfbytesRead)))
        {
            // Free the memory for the buffer, m_cBuf.GetData() will return NULL
            m_cBuf.RemoveAll();

            log::Error(_L_, hr, L"Failed to read at offset %llu (%s)\n", m_ulChunkOffset, m_DiskInterface.c_str());
            return hr;
        }
        bytesSuccessfullyRead = numberOfbytesRead;
    }

    if (getReadingTime && !QueryPerformanceCounter(&perfCountAfter))
    {
        log::Info(_L_, L"QueryPerformanceCounter after reading at offset %llu failed", m_ulChunkOffset);
        getReadingTime = false;
    }

    if (getReadingTime)
    {
        m_readingTime = perfCountAfter.QuadPart - perfCountBefore.QuadPart;
    }

    if (bytesSuccessfullyRead != m_ulChunkSize)
    {
        log::Warning(
            _L_,
            hr,
            L"Failed to read all of the %lu bytes at offset %llu, only %lu bytes have been read (%s)\n",
            m_ulChunkSize,
            m_ulChunkOffset,
            bytesSuccessfullyRead,
            m_DiskInterface.c_str());
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
    LocationSet aSet(_L_);

    if (auto hr = aSet.EnumerateLocations(); FAILED(hr))
    {
        log::Error(_L_, hr, L"[GetSectorsCmd::readAtVolumeLevel] Failed to enumerate locations\r\n");
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

        for (const auto& location : volumeLocations.Locations)
        {
            if (found)
            {
                log::Info(
                    _L_,
                    L"[GetSectorsCmd::readAtVolumeLevel] Higher level access for %s : %s of type %d\r\n",
                    diskName.c_str(),
                    location->GetLocation().c_str(),
                    location->GetType());
                if (location->GetType() == Location::Type::MountedVolume)
                {
                    if ((offset > volOffset) && (offset < (volOffset + volSize)))
                    {
                        log::Info(
                            _L_,
                            L"[GetSectorsCmd::readAtVolumeLevel] Reading using the device %s to bypass potential disk "
                            L"encryption (for partition at offset %llu of disk %s).\r\n",
                            location->GetLocation().c_str(),
                            volOffset,
                            diskName.c_str());
                        return std::make_shared<DiskChunkStream>(
                            _L_, diskName, offset - volOffset, size, description, location->GetLocation());
                    }
                }
            }
            else if (extractInfoFromLocation(location, volDeviceName, volOffset, volSize))
            {
                if ((_wcsicmp(diskName.c_str(), volDeviceName.c_str()) == 0) && (offset > volOffset)
                    && (offset < (volOffset + volSize)))
                {
                    // The data we want to read is located in this volume
                    log::Info(
                        _L_,
                        L"[GetSectorsCmd::readAtVolumeLevel] Found %s by using altitude selector. Now trying to get a "
                        L"higher level access.\r\n",
                        location->GetLocation().c_str());
                    found = true;
                }
            }
        }
    }

    log::Info(
        _L_, L"[GetSectorsCmd::readAtVolumeLevel] Failed to get a higher access level for %s\r\n", diskName.c_str());
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
    PartitionTable partTable(_L_);
    if (FAILED(hr = partTable.LoadPartitionTable(diskName.c_str())))
    {
        log::Error(_L_, hr, L"Failed to load partition table\r\n");
        return hr;
    }

    if (partTable.Table().empty())
    {
        log::Warning(_L_, hr = E_UNEXPECTED, L"Partition Table empty\r\n");
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
                    log::Warning(
                        _L_,
                        hr,
                        L"UEFI partition too large, the dump will be truncated to %d bytes (partition start offset "
                        L"%llu, real size %llu bytes).",
                        efiPartitionSizeToDump,
                        partition.Start,
                        partition.Size);
                }
                else
                {
                    efiPartitionSizeToDump = (DWORD)partition.Size;
                }

                dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                    _L_, diskName, partition.Start, efiPartitionSizeToDump, L"EFI-partition", diskInterfaceToRead));
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
        std::make_shared<DiskChunkStream>(_L_, diskName, 0, MBR_SIZE_IN_BYTES, L"MBR", diskInterfaceToRead));

    std::shared_ptr<DiskChunkStream> vbrChunk, vbrBackupChunk;
    DWORD64 hiddenSectors = 0, hiddenSectorsVbrBackup = 0;
    std::shared_ptr<DiskChunkStream> iplChunk, iplChunk2;
    UINT iplSizeInBytes = 0;
    ULONGLONG iplOffset = 0;
    std::wstring description, partitionDesc;

    // Iterate over partitions
    for (const auto& partition : partTable.Table())
    {
        log::Info(
            _L_,
            L"Partition number %u, Type %u, Flags %u, SectorSize %u, Start %llu, End %llu, Size %llu\r\n",
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
                _L_, diskName, partition.Start, VBR_SIZE_IN_BYTES, description.c_str(), diskInterfaceToRead);
            dumpedChunks.push_back(vbrChunk);

            /*
             *  Dump potential VBR backup
             */
            description = L"VBR backup of " + partitionDesc;

            if (partTable.isGPT())
            {
                // VBR should be located just after the end of the NTFS partition
                vbrBackupChunk = std::make_shared<DiskChunkStream>(
                    _L_,
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
                    _L_,
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
                    log::Info(
                        _L_,
                        L"Cannot parse VBR of partition number %u (%s)\r\n",
                        partition.PartitionNumber,
                        diskName.c_str());
                }
                else if (reinterpret_cast<BYTE*>(vbr)[0x1FE] == 0x55 && reinterpret_cast<BYTE*>(vbr)[0x1FF] == 0xAA)
                {
                    hiddenSectors = vbr->PackedBpb.HiddenSectors;
                    log::Info(
                        _L_,
                        L"VBR hidden sector value for partition number %u : %llu (%s)\r\n",
                        partition.PartitionNumber,
                        hiddenSectors,
                        diskName.c_str());

                    iplSizeInBytes = IPL_SIZE_IN_SECTORS * partition.SectorSize;
                    iplOffset = hiddenSectors * partition.SectorSize + partition.SectorSize;

                    description = L"IPL of " + partitionDesc;
                    dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                        _L_, diskName, iplOffset, iplSizeInBytes, description.c_str(), diskInterfaceToRead));

                    if (iplOffset != (partition.Start + partition.SectorSize))
                    {
                        description = L"Data at offset where IPL is usually located for " + partitionDesc;
                        auto dataAtUsualIplPlace = std::make_shared<DiskChunkStream>(
                            _L_,
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
                    log::Info(
                        _L_,
                        L"Invalid VBR for partition number %u (%s)\r\n",
                        partition.PartitionNumber,
                        diskName.c_str());
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
                    log::Info(
                        _L_,
                        L"Cannot parse VBR backup of partition number %u (%s)\r\n",
                        partition.PartitionNumber,
                        diskName.c_str());
                }
                else if (reinterpret_cast<BYTE*>(vbr)[0x1FE] == 0x55 && reinterpret_cast<BYTE*>(vbr)[0x1FF] == 0xAA)
                {
                    hiddenSectorsVbrBackup = vbr->PackedBpb.HiddenSectors;
                    log::Info(
                        _L_,
                        L"VBR backup hidden sector value for partition number %u : 0x%llu (%s)\r\n",
                        partition.PartitionNumber,
                        hiddenSectorsVbrBackup,
                        diskName.c_str());

                    // Only dump IPL at this offset if it is different from the one found in normal VBR
                    if (hiddenSectorsVbrBackup != hiddenSectors)
                    {
                        iplSizeInBytes = IPL_SIZE_IN_SECTORS * partition.SectorSize;
                        iplOffset = hiddenSectorsVbrBackup * partition.SectorSize + partition.SectorSize;

                        description = L"IPL of " + partitionDesc + L" by following VBR backup";
                        dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                            _L_, diskName, iplOffset, iplSizeInBytes, description.c_str(), diskInterfaceToRead));
                    }
                }
                else
                {
                    log::Info(
                        _L_,
                        L"Invalid VBR backup for partition number %u (%s)\r\n",
                        partition.PartitionNumber,
                        diskName.c_str());
                }
            }
        }
    }

    return S_OK;
}

HRESULT Main::DumpRawGPT(const std::wstring& diskName, const std::wstring& diskInterfaceToRead)
{
    HRESULT hr = E_FAIL;
    CDiskExtent diskExtent(_L_, diskInterfaceToRead);
    LARGE_INTEGER offsetToRead;
    ULONGLONG dwBytesRead = 0;
    ULONG sectorSize;

    if (FAILED(hr = diskExtent.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        log::Error(_L_, hr, L"Could not open Location %s\r\n", diskExtent.GetName().c_str());
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
        _L_, diskName, offsetToRead.QuadPart, sectorSize, L"GPT-primary-header", diskInterfaceToRead);
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
            log::Error(_L_, hr, L"[GetSectorsCmd::DumpRawGPT] Bad GPT signature\r\n");
            return hr;
        }

        // check validity of some parameters
        if (gptHeader->NumberOfPartitionEntries > 128)
        {
            log::Error(
                _L_,
                hr,
                L"Abnormaly large number of GPT partition entries (%d)\r\n",
                gptHeader->NumberOfPartitionEntries);
            return hr;
        }

        if (gptHeader->SizeofPartitionEntry > 0x10000)
        {
            log::Error(_L_, hr, L"Abnormaly large partition entry (%d)\r\n", gptHeader->SizeofPartitionEntry);
            return hr;
        }

        // Dump partition table entries
        dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
            _L_,
            diskName,
            gptHeader->PartitionEntryLba * sectorSize,
            gptHeader->NumberOfPartitionEntries * gptHeader->SizeofPartitionEntry,
            L"GPT-primary-partition-entries",
            diskInterfaceToRead));

        /*
        // Get the secondary GPT
        */
        auto gptHeaderSecondary = std::make_shared<DiskChunkStream>(
            _L_,
            diskName,
            gptHeader->AlternativeLba * sectorSize,
            sectorSize,
            L"GPT-secondary-header",
            diskInterfaceToRead);
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
                log::Error(_L_, hr, L"[GetSectorsCmd::DumpRawGPT] Bad GPT signature (secondary GPT)\r\n");
                return hr;
            }

            // check validity of some parameters
            if (gptHeader->NumberOfPartitionEntries > 128)
            {
                log::Error(
                    _L_,
                    hr,
                    L"Abnormaly large number of GPT partition entries (%d) (secondary GPT)\r\n",
                    gptHeader->NumberOfPartitionEntries);
                return hr;
            }

            if (gptHeader->SizeofPartitionEntry > 0x10000)
            {
                log::Error(
                    _L_,
                    hr,
                    L"Abnormaly large partition entry (%d) (secondary GPT)\r\n",
                    gptHeader->SizeofPartitionEntry);
                return hr;
            }

            // Dump partition table entries
            dumpedChunks.push_back(std::make_shared<DiskChunkStream>(
                _L_,
                diskName,
                gptHeader->PartitionEntryLba * sectorSize,
                gptHeader->NumberOfPartitionEntries * gptHeader->SizeofPartitionEntry,
                L"GPT-secondary-partition-entries",
                diskInterfaceToRead));
        }
    }

    return S_OK;
}

bool partitionSortByStartAddress(std::map<string, ULONGLONG>& item1, std::map<string, ULONGLONG>& item2)
{
    return item1["start"] < item2["start"];
}

HRESULT Main::DumpSlackSpace(const std::wstring& diskName, const std::wstring& diskInterfaceToRead)
{
    HRESULT hr = E_FAIL;
    PVOID pBuf = NULL;
    PartitionTable pt(_L_);

    std::vector<std::map<string, ULONGLONG>> usedLocations;
    std::map<string, ULONGLONG> loc;
    ULONGLONG ulSlackSpaceOffset = 0;
    DWORD slackSpaceSize = 0;

    if (FAILED(hr = pt.LoadPartitionTable(diskName.c_str())))
    {
        log::Error(_L_, hr, L"Failed to load partition table\r\n");
        return hr;
    }

    if (pt.Table().empty())
    {
        log::Warning(_L_, E_UNEXPECTED, L"Partition Table empty\r\n");
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
    CDiskExtent diskReader(_L_, diskInterfaceToRead);
    ULONGLONG diskSize = 0;
    if (FAILED(hr = diskReader.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING)))
    {
        log::Error(_L_, hr, L"Failed to open a disk for read access (%s)\n", diskInterfaceToRead.c_str());
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
                _L_, diskName, ulSlackSpaceOffset, slackSpaceSize, L"Disk slack space", diskInterfaceToRead);
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
        _L_, diskName, config.customSampleOffset, config.customSampleSize, L"Custom sample", diskInterfaceToRead));
    return S_OK;
}

HRESULT Main::CollectDiskChunks(const OutputSpec& output)
{
    HRESULT hr = E_FAIL;

    switch (output.Type)
    {
        case OutputSpec::Archive:
        {
            auto compressor = ArchiveCreate::MakeCreate(config.Output.ArchiveFormat, _L_, true);

            auto [hr, CSV] = CreateDiskChunkArchiveAndCSV(config.Output.Path, compressor);

            if (FAILED(hr))
                return hr;

            for (auto& diskChunk : dumpedChunks)
            {
                // Make sure we read from the beginning of the chunk
                diskChunk->SetFilePointer(0, FILE_BEGIN, NULL);

                hr = CollectDiskChunk(compressor, CSV->GetTableOutput(), diskChunk);
                if
                    FAILED(hr)
                    {
                        log::Error(_L_, hr, L"Unable to archive result for %s\r\n", diskChunk->m_description.c_str());
                    }
            }
            FinalizeArchive(compressor, CSV);
        }
        break;
        case OutputSpec::Directory:
        {
            auto [hr, CSV] = CreateOutputDirLogFileAndCSV(config.Output.Path);

            if (FAILED(hr))
                return hr;

            for (auto& diskChunk : dumpedChunks)
            {
                // Make sure we read from the beginning of the chunk
                diskChunk->SetFilePointer(0, FILE_BEGIN, NULL);

                hr = CollectDiskChunk(config.Output.Path, CSV->GetTableOutput(), diskChunk);
                if
                    FAILED(hr)
                    {
                        log::Error(_L_, hr, L"Unable to archive result for %s\r\n", diskChunk->m_description.c_str());
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

    auto logStream = std::make_shared<TemporaryStream>(_L_);

    if (FAILED(hr = logStream->Open(tempdir.wstring(), L"GetSectors", 5 * 1024 * 1024)))
    {
        log::Error(_L_, hr, L"Failed to create temp stream\r\n");
        return {hr, nullptr};
    }
    if (FAILED(hr = _L_->LogToStream(logStream)))
    {
        log::Error(_L_, hr, L"Failed to initialize temp logging\r\n");
        return {hr, nullptr};
    }

    auto csvStream = std::make_shared<TemporaryStream>(_L_);

    if (FAILED(hr = csvStream->Open(tempdir.wstring(), L"GetSectors", 1 * 1024 * 1024)))
    {
        log::Error(_L_, hr, L"Failed to create temp stream\r\n");
        return {hr, nullptr};
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;

    auto CSV = TableOutput::CSV::Writer::MakeNew(_L_, std::move(options));

    if (FAILED(hr = CSV->WriteToStream(csvStream)))
    {
        log::Error(_L_, hr, L"Failed to initialize CSV stream\r\n");
        return {hr, nullptr};
    }
    CSV->SetSchema(config.Output.Schema);

    if (FAILED(hr = compressor->InitArchive(pArchivePath.c_str())))
    {
        log::Error(_L_, hr, L"Failed to initialize cabinet file %s\r\n", pArchivePath.c_str());
        return {hr, nullptr};
    }

    compressor->SetCallback([this](const Archive::ArchiveItem& item) {
        log::Info(_L_, L"\tFile archived : %s\r\n", item.NameInArchive.c_str());
    });

    return {S_OK, std::move(CSV)};
}

std::pair<HRESULT, std::shared_ptr<TableOutput::IWriter>>
Main::CreateOutputDirLogFileAndCSV(const std::wstring& strOutputDir)
{
    HRESULT hr = E_FAIL;

    if (strOutputDir.empty())
        return {E_POINTER, nullptr};

    fs::path outDir(strOutputDir);

    switch (fs::status(outDir).type())
    {
        case fs::file_type::not_found:
            if (create_directories(outDir))
            {
                log::Verbose(_L_, L"Created output directory %s\r\n", strOutputDir.c_str());
            }
            else
            {
                log::Verbose(_L_, L"Output directory %s already exists\r\n", strOutputDir.c_str());
            }
            break;
        case fs::file_type::directory:
            log::Verbose(_L_, L"Specified output directory %s exists and is a directory\r\n", strOutputDir.c_str());
            break;
        default:
            log::Error(
                _L_,
                E_INVALIDARG,
                L"Specified output directory %s exists and is not a directory\r\n",
                strOutputDir.c_str());
            break;
    }

    fs::path logFile;

    logFile = outDir;
    logFile /= L"GetSectors.log";

    if (!_L_->IsLoggingToFile())
    {
        if (FAILED(hr = _L_->LogToFile(logFile.wstring().c_str())))
            return {hr, nullptr};
    }

    fs::path csvFile(outDir);
    csvFile /= L"GetSectors.csv";

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = config.Output.OutputEncoding;

    auto CSV = TableOutput::CSV::Writer::MakeNew(_L_, std::move(options));

    if (FAILED(hr = CSV->WriteToFile(csvFile.wstring().c_str())))
        return {hr, nullptr};

    CSV->SetSchema(config.Output.Schema);

    return {S_OK, std::move(CSV)};
}

HRESULT Main::CollectDiskChunk(
    const std::shared_ptr<ArchiveCreate>& compressor,
    ITableOutput& output,
    std::shared_ptr<DiskChunkStream> diskChunk)
{
    HRESULT hr = E_FAIL;
    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);

    if (FAILED(hr = compressor->AddStream(diskChunk->getSampleName().c_str(), L"dummy", diskChunk)))
    {
        // note : pas d'erreur lorsqu'un sample avec un nom bizarre n'a pas été mis dans dans un zip
        log::Error(_L_, hr, L"Failed to add a sample to the archive\r\n");
    }
    else
    {
        if (FAILED(hr = AddDiskChunkRefToCSV(output, strComputerName, *diskChunk)))
        {
            log::Error(_L_, hr, L"Failed to add a sample metadata to csv\r\n");
        }
        if (FAILED(hr = compressor->FlushQueue()))
        {
            log::Error(_L_, hr, L"Failed to flush queue to %s\r\n", config.Output.Path.c_str());
            return hr;
        }
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
            if (FAILED(hr = pCSVStream->SetFilePointer(0, FILE_BEGIN, nullptr)))
            {
                log::Error(_L_, hr, L"Failed to rewind csv stream\r\n");
            }
            if (FAILED(hr = compressor->AddStream(L"GetSectors.csv", L"GetSectors.csv", pCSVStream)))
            {
                log::Error(_L_, hr, L"Failed to add GetSectors.csv\r\n");
            }
        }
    }

    // Archive log file
    auto pLogStream = _L_->GetByteStream();
    _L_->CloseLogToStream(false);
    if (pLogStream && pLogStream->GetSize() > 0LL)
    {
        if (FAILED(hr = pLogStream->SetFilePointer(0, FILE_BEGIN, nullptr)))
        {
            log::Error(_L_, hr, L"Failed to rewind log stream\r\n");
        }
        if (FAILED(hr = compressor->AddStream(L"GetSectors.log", L"GetSectors.log", pLogStream)))
        {
            log::Error(_L_, hr, L"Failed to add GetSectors.log\r\n");
        }
    }

    if (FAILED(hr = compressor->Complete()))
    {
        log::Error(_L_, hr, L"Failed to complete %s\r\n", config.Output.Path.c_str());
        return hr;
    }

    CSV->Close();
    return S_OK;
}

HRESULT
Main::CollectDiskChunk(const std::wstring& outputdir, ITableOutput& output, std::shared_ptr<DiskChunkStream> diskChunk)
{
    HRESULT hr = E_FAIL;
    fs::path output_dir(outputdir);
    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);
    fs::path sampleFile = output_dir / fs::path(diskChunk->getSampleName());
    FileStream outputStream(_L_);

    ULONGLONG ullBytesWritten = 0LL, ullBytesRead = 0LL;

    // TODO : read by chunks
    CBinaryBuffer cBuf;
    cBuf.SetCount((size_t)diskChunk->GetSize());
    cBuf.ZeroMe();

    hr = diskChunk->Read(cBuf.GetData(), cBuf.GetCount(), &ullBytesRead);
    if (hr != S_OK)
    {
        return hr;
    }

    if (FAILED(hr = outputStream.WriteTo(sampleFile.wstring().c_str())))
    {
        log::Error(_L_, hr, L"Failed to create sample file %s\r\n", sampleFile.wstring().c_str());
        return hr;
    }

    if (FAILED(hr = outputStream.Write(cBuf.GetData(), cBuf.GetCount(), &ullBytesWritten)))
    {
        log::Error(_L_, hr, L"Failed while writing to sample %s\r\n", sampleFile.string().c_str());
        return hr;
    }

    outputStream.Close();

    if (FAILED(hr = AddDiskChunkRefToCSV(output, strComputerName, *diskChunk)))
    {
        log::Error(_L_, hr, L"Failed to add diskChunk metadata to csv\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT
Main::AddDiskChunkRefToCSV(ITableOutput& output, const std::wstring& strComputerName, DiskChunkStream& diskChunk)
{

    HRESULT hr = E_FAIL;

    output.WriteString(strComputerName.c_str());
    output.WriteString(diskChunk.m_DiskName.c_str());
    output.WriteString(diskChunk.m_description.c_str());
    output.WriteString(diskChunk.getSampleName().c_str());
    output.WriteInteger(diskChunk.m_offset);
    output.WriteInteger(diskChunk.m_size);
    output.WriteInteger(diskChunk.m_readingTime);
    output.WriteString(diskChunk.m_DiskInterface.c_str());
    output.WriteInteger(diskChunk.m_diskReader->GetLogicalSectorSize());
    output.WriteEndOfLine();

    return S_OK;
}
