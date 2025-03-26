//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "PartitionTable.h"

#include <sstream>
#include <locale>
#include <codecvt>

#include "Log/Log.h"

#include "DiskStructures.h"
#include "FSVBR.h"
#include "DiskExtent.h"
#include "NtfsDataStructures.h"
#include "diskguid.h"

#include "Text/Fmt/Partition.h"

#include "Text/Fmt/Partition.h"

using namespace Orc;

static const unsigned char MBR_SIGNATURE[2] = {0x55, 0xAA};
static const std::string GPT_SIGNATURE("EFI PART");

PartitionTable::PartitionTable()
{
    m_pDiskExtent = NULL;
}

PartitionTable::~PartitionTable(void)
{
    if (m_pDiskExtent != NULL)
    {
        m_pDiskExtent->Close();
    }
}

HRESULT PartitionTable::LoadPartitionTable(const WCHAR* szDeviceOrImage)
{
    if (m_pDiskExtent != NULL)
    {
        m_pDiskExtent->Close();
    }

    m_pDiskExtent = new CDiskExtent(szDeviceOrImage);
    return LoadPartitionTable(*m_pDiskExtent);
}

HRESULT PartitionTable::LoadPartitionTable(IDiskExtent& diskExtent)
{
    HRESULT hr = E_FAIL;

    // open disk extent
    if (FAILED(OpenDiskExtend(diskExtent)))
        return hr;

    // read data at offset 0
    CBinaryBuffer buffer;
    buffer.SetCount(sizeof(MasterBootRecord));

    if (FAILED(ReadDiskExtend(diskExtent, buffer)))
        return hr;

    // parse MBR
    PMasterBootRecord pMBR = (PMasterBootRecord)buffer.GetData();

    if (pMBR->MBRSignature[0] == MBR_SIGNATURE[0] && pMBR->MBRSignature[1] == MBR_SIGNATURE[1])
    {
        // get sector size
        UINT sectorSize = GetSectorSize(diskExtent);

        Log::Trace(L"Parsing MBR partition table for '{}'", diskExtent.GetName());
        return ParseMBRPartitionTable(diskExtent, sectorSize, pMBR);
    }
    else
    {
        Log::Error(L"Not a valid MBR signature in '{}'", diskExtent.GetName());
        return E_INVALIDARG;
    }
}

/*
 * guess partition type (refs, ntfs or fat) based on the first sector of the volume
 * static method
 */
HRESULT PartitionTable::GuessPartitionType(const CBinaryBuffer& buffer, Partition& p)
{
    FSVBR::FSType fsType = FSVBR::GuessFSType(buffer);

    switch (fsType)
    {
        case FSVBR::FSType::NTFS:
            p.PartitionType = Partition::Type::NTFS;
            break;
        case FSVBR::FSType::FAT12:
            p.PartitionType = Partition::Type::FAT12;
            break;
        case FSVBR::FSType::FAT16:
            p.PartitionType = Partition::Type::FAT16;
            break;
        case FSVBR::FSType::FAT32:
            p.PartitionType = Partition::Type::FAT32;
            break;
        case FSVBR::FSType::REFS:
            p.PartitionType = Partition::Type::REFS;
            break;
        case FSVBR::FSType::BITLOCKER:
            p.PartitionType = Partition::Type::BitLocked;
            break;
        default:
            p.PartitionType = Partition::Type::Other;
            break;
    }
    return S_OK;
}

/*
 * parse a diskPartition structure and fill out a partition object
 * static method
 */
void PartitionTable::ParseDiskPartition(
    DiskPartition* diskPartition,
    UINT sectorSize,
    Partition& p,
    ULONG64 partitionOffset)
{
    p.SectorSize = sectorSize;

    // partition start offset & size
    p.Start = ((ULONG64)diskPartition->SectorAddress) * sectorSize + partitionOffset;
    p.Size = ((ULONG64)diskPartition->NumberOfSector) * sectorSize;
    p.End = p.Start + p.Size;

    // field flag
    if (diskPartition->Flag == 0x80)
    {
        p.PartitionFlags = Partition::Flags::Bootable;
    }
    else if (diskPartition->Flag != 0x00)
    {
        p.PartitionFlags = Partition::Flags::Invalid;
    }

    if (p.PartitionFlags != Partition::Flags::Invalid)
    {
        // field sysflag
        p.SysFlags = diskPartition->SysFlag;

        switch (p.SysFlags)
        {
            case 0x7:
                p.PartitionType = Partition::Type::NTFS;
                break;
            case 0x5:
            case 0xF:
                p.PartitionType = Partition::Type::Extended;
                break;
            case 0xB:
            case 0xC:
                p.PartitionType = Partition::Type::FAT32;
                break;
            case 0xEE:
                p.PartitionType = Partition::Type::GPT;
                break;
            case 0x0:
                p.PartitionType = Partition::Type::Invalid;
                break;
            default:
                p.PartitionType = Partition::Type::Other;
        }
    }
}

/*
 * parse a gptPartitionEntry structure and fill out a partition object
 * will guess partition type by reading the provided disk extent
 * static method
 */
void PartitionTable::ParseDiskPartition(
    IDiskExtent& extent,
    GPTPartitionEntry* gptPartitionEntry,
    UINT sectorSize,
    Partition& p)
{
    bool guessPartitionType = false;

    if (!memcmp(&gptPartitionEntry->PartitionType, &PARTITION_ENTRY_UNUSED_GUID, sizeof(GUID)))
    {
        return;  // NULL GUID, invalid entry
    }

    if (!memcmp(&gptPartitionEntry->PartitionType, &PARTITION_SYSTEM_GUID, sizeof(GUID)))
    {
        p.PartitionType = Partition::Type::ESP;
    }
    else if (!memcmp(&gptPartitionEntry->PartitionType, &PARTITION_MSFT_RESERVED_GUID, sizeof(GUID)))
    {
        p.PartitionType = Partition::Type::MICROSOFT_RESERVED;
    }
    else
    {
        p.PartitionType = Partition::Type::Other;
        guessPartitionType = true;
    }

    p.SectorSize = sectorSize;

    ULONG64 start = gptPartitionEntry->FirstLba * sectorSize;
    ULONG64 end = gptPartitionEntry->LastLba * sectorSize;

    p.Start = start;
    p.End = end;
    p.Size = 0;

    if (end > start)
    {
        p.Size = end - start;
    }

    if (guessPartitionType)
    {
        LARGE_INTEGER offset;
        LARGE_INTEGER newpos;
        offset.QuadPart = p.Start;

        if (extent.Seek(offset, &newpos, FILE_BEGIN) == S_OK)
        {
            CBinaryBuffer buffer;
            if (!buffer.SetCount(sectorSize))
            {
                p.PartitionFlags = Partition::Flags::None;
                return;
            }
            DWORD dwBytesRead = 0;

            if (extent.Read(buffer.GetData(), (DWORD)buffer.GetCount(), &dwBytesRead) == S_OK)
            {
                GuessPartitionType(buffer, p);
            }
        }
    }

    // flags
    struct Flags
    {
        std::int64_t b1 : 1, : 1, b2 : 1, : 45, : 13, b3 : 1, b4 : 1, b5 : 1;
    };

    Flags f = {gptPartitionEntry->Flags};
    std::int32_t flags = (std::int32_t)Partition::Flags::None;

    if (f.b1)  // system
    {
        flags |= (std::int32_t)Partition::Flags::System;
    }

    if (f.b2)  // legacy bios bootable
    {
        flags |= (std::int32_t)Partition::Flags::Bootable;
    }

    if (f.b3)  // read only
    {
        flags |= (std::int32_t)Partition::Flags::ReadOnly;
    }

    if (f.b4)  // hidden
    {
        flags |= (std::int32_t)Partition::Flags::Hidden;
    }

    if (f.b5)  // noautomount
    {
        flags |= (std::int32_t)Partition::Flags::NoAutoMount;
    }

    p.PartitionFlags = static_cast<Partition::Flags>(flags);
}

/*
 * try to determine disk geometry, default to 512 if fails
 * static method
 */
UINT PartitionTable::GetSectorSize(IDiskExtent& extent)
{
    DISK_GEOMETRY Geometry;

    UINT sectorSize = 512;
    DWORD dwOutBytes = 0;

    if (extent.GetHandle() != INVALID_HANDLE_VALUE
        && DeviceIoControl(
            extent.GetHandle(),
            IOCTL_DISK_GET_DRIVE_GEOMETRY,
            NULL,
            0,
            &Geometry,
            sizeof(DISK_GEOMETRY),
            &dwOutBytes,
            NULL))
    {
        if (Geometry.BytesPerSector > 0)
            sectorSize = Geometry.BytesPerSector;
    }

    return sectorSize;
}

/*
 * private methods
 */

/*
 * MBR partition table
 */
HRESULT PartitionTable::ParseMBRPartitionTable(IDiskExtent& diskExtend, UINT sectorSize, PMasterBootRecord pMBR)
{
    HRESULT hr = E_FAIL;

    for (UINT i = 0; i < MAX_BASIC_PARTITION; i++)
    {
        DiskPartition* pPartition = &pMBR->PartitionTable[i];
        Partition p(GetPartitionNumber());

        Log::Trace(L"Parsing partition entry #{}", i);
        ParseDiskPartition(pPartition, sectorSize, p, 0);

        if (p.IsExtented())
        {
            CBinaryBuffer buffer;
            buffer.SetCount(sizeof(ExtendedBootRecord));

            if (FAILED(SeekDiskExtend(diskExtend, p.Start)))
                return hr;

            if (FAILED(ReadDiskExtend(diskExtend, buffer)))
                return hr;

            Log::Trace(L"Found an extended partition. Parsing extended partition");

            if (FAILED(hr = ParseExtendedPartition(diskExtend, p, (ExtendedBootRecord*)buffer.GetData(), p.Start)))
            {
                Log::Error(L"Failed to parse extended partition of {} [{}]", diskExtend.GetName(), SystemError(hr));
                return hr;
            }
        }
        else if (p.IsGPT())
        {
            // protective MBR -> there must be a GPT partition table right after
            // so we read the 2nd sector
            Log::Trace(L"Found a GPT partition. Parsing now GPT partition table");

            CBinaryBuffer buffer;
            buffer.SetCount(sizeof(GPTHeader));

            if (FAILED(SeekDiskExtend(diskExtend, sectorSize)))
                return hr;

            if (FAILED(ReadDiskExtend(diskExtend, buffer)))
                return hr;

            PGPTHeader gptHeader = reinterpret_cast<PGPTHeader>(buffer.GetData());

            // try first partition table
            if (FAILED(hr = ParseGPTPartitionTable(diskExtend, sectorSize, gptHeader)))
            {
                Log::Error(
                    L"Failed to parse GPT partition of {} using primary GPT header. Will now try backup GPT header "
                    L"[{}]",
                    diskExtend.GetName(),
                    SystemError(hr));

                // try second partition table
                if (FAILED(SeekDiskExtend(diskExtend, gptHeader->AlternativeLba * sectorSize)))
                {
                    return hr;
                }

                if (FAILED(ReadDiskExtend(diskExtend, buffer)))
                    return hr;

                if (FAILED(
                        hr = ParseGPTPartitionTable(
                            diskExtend, sectorSize, reinterpret_cast<PGPTHeader>(buffer.GetData()))))
                {
                    Log::Error(
                        L"Failed to parse GPT partition of {} using backup GPT header [{}]",
                        diskExtend.GetName(),
                        SystemError(hr));
                    return hr;
                }
            }
        }
        else if (p.IsValid())
        {
            AddPartition(p);
        }
    }

    return S_OK;
}

/*
 * GPT partition table
 */
HRESULT PartitionTable::ParseGPTPartitionTable(IDiskExtent& diskExtend, UINT sectorSize, PGPTHeader pGPTHeader)
{
    HRESULT hr = E_FAIL;

    // check GPT signature
    if (std::strncmp(reinterpret_cast<char*>(pGPTHeader->Signature), GPT_SIGNATURE.c_str(), 8))
    {
        Log::Error(L"Bad GPT signature");
        return hr;
    }

    // header crc check
    DWORD headerCrc = pGPTHeader->Crc32;
    pGPTHeader->Crc32 = 0;  // this field must be set to 0 when computing crc

    CBinaryBuffer headerBuffer(reinterpret_cast<LPBYTE>(pGPTHeader), pGPTHeader->Size);

    if (headerCrc != Crc32(headerBuffer))
    {
        Log::Error(L"Bad CRC32 for GPT partition header");
        return hr;
    }

    // check validity of some parameters
    if (pGPTHeader->NumberOfPartitionEntries > 128)
    {
        Log::Warn("Abnormally large number of GPT partition entries: {}", pGPTHeader->NumberOfPartitionEntries);
    }

    CBinaryBuffer buffer;
    size_t buffer_size = 0;

    if (!msl::utilities::SafeMultiply(
            (size_t)pGPTHeader->NumberOfPartitionEntries, pGPTHeader->SizeofPartitionEntry, buffer_size))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    if (!buffer.SetCount(buffer_size))
        return E_OUTOFMEMORY;

    if (FAILED(SeekDiskExtend(diskExtend, pGPTHeader->PartitionEntryLba * sectorSize)))
        return hr;

    if (FAILED(ReadDiskExtend(diskExtend, buffer)))
        return hr;

    // check partition table CRC32
    DWORD tableCrc = Crc32(buffer);

    if (tableCrc != pGPTHeader->PartitionEntryArrayCrc32)
    {
        Log::Warn("Bad CRC32 for GPT partition table");
    }

    LONGLONG offset = 0;

    for (unsigned int i = 0; i < pGPTHeader->NumberOfPartitionEntries; i++)
    {
        GPTPartitionEntry* pGPTPartitionEntry = reinterpret_cast<GPTPartitionEntry*>(buffer.GetData() + offset);
        Partition p(GetPartitionNumber());
        ParseDiskPartition(diskExtend, pGPTPartitionEntry, sectorSize, p);

        if (p.IsValid())
        {
            AddPartition(p);
        }

        offset += pGPTHeader->SizeofPartitionEntry;
    }

    m_isGPT = true;

    return S_OK;
}

/*
 * ExtendedPartition
 * recursive function
 */
HRESULT PartitionTable::ParseExtendedPartition(
    IDiskExtent& diskExtend,
    const Partition& p,
    ExtendedBootRecord* pExtendedRecord,
    ULONG64 u64ExtendedPartitionStart)
{
    HRESULT hr = E_FAIL;

    if (pExtendedRecord->MBRSignature[1] != 0xAA && pExtendedRecord->MBRSignature[0] != 0x55)
    {
        Log::Warn(
            "Extended partition's signature failed to verify ({:#x}{:#x}) --> Partition ignored",
            pExtendedRecord->MBRSignature[0],
            pExtendedRecord->MBRSignature[1]);
        return S_OK;
    }

    DiskPartition* pExtendedPartition = &pExtendedRecord->PartitionTable[0];
    Partition ep(GetPartitionNumber());

    ParseDiskPartition(pExtendedPartition, p.SectorSize, ep, u64ExtendedPartitionStart);

    if (ep.IsValid())
    {
        AddPartition(ep);
    }
    DiskPartition* pNextExtendedPartition = &pExtendedRecord->PartitionTable[1];

    if (pNextExtendedPartition->NumberOfSector == 0)  // Last extended part read;
        return S_OK;

    Log::Trace("Found an extended partition. Parsing extended partition");

    //
    // Not the last, continue parsing (recursively)
    //

    CBinaryBuffer buffer;
    buffer.SetCount(sizeof(ExtendedBootRecord));
    ULONGLONG offset = p.Start + (pNextExtendedPartition->SectorAddress * p.SectorSize);

    if (FAILED(SeekDiskExtend(diskExtend, offset)))
        return hr;

    if (FAILED(ReadDiskExtend(diskExtend, buffer)))
        return hr;

    ExtendedBootRecord* pNextExtendedRecord = (ExtendedBootRecord*)buffer.GetData();

    if (FAILED(hr = ParseExtendedPartition(diskExtend, p, pNextExtendedRecord, offset)))
    {
        Log::Error("Failed to parse extended partition [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

void PartitionTable::AddPartition(const Orc::Partition& p)
{
    m_Table.push_back(p);

    Log::Trace(L"Partition added: {}", p);
}

UINT8 PartitionTable::GetPartitionNumber()
{
    return (UINT8)m_Table.size() + 1;
}

HRESULT PartitionTable::OpenDiskExtend(IDiskExtent& diskExtend)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = diskExtend.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        Log::Error(L"Could not open Location '{}' [{}]", diskExtend.GetName(), SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT PartitionTable::SeekDiskExtend(IDiskExtent& diskExtend, ULONG64 offset)
{
    HRESULT hr = E_FAIL;
    LARGE_INTEGER off;
    LARGE_INTEGER newpos;

    off.QuadPart = offset;

    if (FAILED(hr = diskExtend.Seek(off, &newpos, FILE_BEGIN)))
    {
        Log::Error(L"Failed to seek from Location '{}' [{}]", diskExtend.GetName(), SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT PartitionTable::ReadDiskExtend(IDiskExtent& diskExtend, CBinaryBuffer& buffer)
{
    HRESULT hr = E_FAIL;
    DWORD dwBytesRead = 0;

    if (FAILED(hr = diskExtend.Read(buffer.GetData(), (DWORD)buffer.GetCount(), &dwBytesRead)))
    {
        Log::Error(L"Failed to read from Location '{}' [{}]", diskExtend.GetName(), SystemError(hr));
        return hr;
    }

    return S_OK;
}

DWORD PartitionTable::Crc32(const CBinaryBuffer& buffer)
{
    static uint32_t crc32_tab[] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832,
        0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a,
        0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
        0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab,
        0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4,
        0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074,
        0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525,
        0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
        0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76,
        0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6,
        0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7,
        0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7,
        0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
        0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330,
        0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

    BYTE* p;
    DWORD crc;
    size_t size = buffer.GetCount();

    p = buffer.GetData();
    crc = ~0U;

    while (size--)
        crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

    return crc ^ ~0U;
}
