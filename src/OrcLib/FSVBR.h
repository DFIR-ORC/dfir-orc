//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "BinaryBuffer.h"

#include "FatDataStructures.h"

#pragma managed(push, off)

namespace Orc {

static const unsigned char VBR_SIGNATURE[2] = {0x55, 0xAA};
static const std::string NTFS_VBR_SIGNATURE("NTFS");
static const std::string REFS_VBR_SIGNATURE("ReFS");
static const std::string FAT_VBR_SIGNATURE("MSDOS");
static const std::string FAT12_VBR_SIGNATURE("FAT12");
static const std::string FAT16_VBR_SIGNATURE("FAT16");
static const std::string FAT32_VBR_SIGNATURE("FAT32");
static const std::string FVE_VBR_SIGNATURE("-FVE-FS");

class ORCLIB_API FSVBR
{
public:
    enum class FSType : std::int32_t
    {
        UNKNOWN = 0,
        FAT12 = 0x1,
        FAT16 = 0x2,
        FAT32 = 0x4,
        FAT = 0x7,
        NTFS = 0x8,
        REFS = 0x10,
        BITLOCKER = 0x20,
        ALL = 0xFFFFFFF
    };

    static boolean IsFSSupported(const std::wstring& fsName)
    {
        if (!fsName.compare(L"NTFS"))
        {
            return true;
        }

        if (!fsName.compare(L"FAT32") || !fsName.compare(L"FAT") || !fsName.compare(L"FAT16")
            || !fsName.compare(L"FAT12"))
        {
            return true;
        }

        // todo : add ReFs!

        return false;
    }

    static FSType GuessFSType(const CBinaryBuffer& buffer)
    {
        PPackedGenBootSector pPGBS = (PPackedGenBootSector)buffer.GetData();
        char* oem = reinterpret_cast<char*>(pPGBS->Oem);

        if (!std::strncmp(oem, REFS_VBR_SIGNATURE.c_str(), REFS_VBR_SIGNATURE.length()))
        {
            return FSType::REFS;
        }
        else
        {
            unsigned char* signature =
                reinterpret_cast<unsigned char*>(pPGBS) + sizeof(PackedGenBootSector) - sizeof(VBR_SIGNATURE);

            // valid check for ntfs & fat32 (but not for refs)
            if (*signature == VBR_SIGNATURE[0] && *(signature + 1) == VBR_SIGNATURE[1])
            {
                // ntfs
                if (!std::strncmp(oem, NTFS_VBR_SIGNATURE.c_str(), NTFS_VBR_SIGNATURE.length()))
                {
                    return FSType::NTFS;
                }
                // fat12/16/32
                else if (!std::strncmp(oem, FAT_VBR_SIGNATURE.c_str(), FAT_VBR_SIGNATURE.length()))
                {
                    // fat12/16
                    PPackedFat1216BootSector pFat1216bs = reinterpret_cast<PPackedFat1216BootSector>(buffer.GetData());

                    if (pFat1216bs->Signature == 0x28 || pFat1216bs->Signature == 0x29)
                    {
                        if (!std::strncmp(
                                reinterpret_cast<char*>(pFat1216bs->SystemId),
                                FAT12_VBR_SIGNATURE.c_str(),
                                FAT12_VBR_SIGNATURE.length()))
                        {
                            return FSType::FAT12;
                        }

                        if (!std::strncmp(
                                reinterpret_cast<char*>(pFat1216bs->SystemId),
                                FAT16_VBR_SIGNATURE.c_str(),
                                FAT16_VBR_SIGNATURE.length()))
                        {
                            return FSType::FAT16;
                        }
                    }

                    // fat32
                    PPackedFat32BootSector pFat32bs = reinterpret_cast<PPackedFat32BootSector>(pPGBS);

                    if (pFat32bs->Signature == 0x28 || pFat32bs->Signature == 0x29)
                    {
                        if (!std::strncmp(
                                reinterpret_cast<char*>(pFat32bs->SystemId),
                                FAT32_VBR_SIGNATURE.c_str(),
                                FAT32_VBR_SIGNATURE.length()))
                        {
                            return FSType::FAT32;
                        }
                    }
                }
                else if (!std::strncmp(oem, FVE_VBR_SIGNATURE.c_str(), FVE_VBR_SIGNATURE.length()))
                {
                    return FSType::BITLOCKER;
                }
            }
        }

        return FSType::UNKNOWN;
    }

    static const std::wstring GetFSName(FSType fsType)
    {
        if (FSVBR::FSType::NTFS == fsType)
        {
            return L"NTFS";
        }
        else if (FSVBR::FSType::FAT12 == fsType)
        {
            return L"FAT12";
        }
        else if (FSVBR::FSType::FAT16 == fsType)
        {
            return L"FAT16";
        }
        else if (FSVBR::FSType::FAT32 == fsType)
        {
            return L"FAT32";
        }
        else if (FSVBR::FSType::REFS == fsType)
        {
            return L"REFS";
        }
        else
        {
            return L"UNKNOWN FS";
        }
    }
};
}  // namespace Orc

#pragma managed(pop)
