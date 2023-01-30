//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"
#include "VolumeReader.h"
#include "FSVBR.h"

#include "FatDataStructures.h"
#include "NtfsDataStructures.h"
#include "RefsDataStructures.h"

using namespace Orc;

VolumeReader::VolumeReader(const WCHAR* szSnapshotName)
{
    wcscpy_s(m_szLocation, ORC_MAX_PATH, szSnapshotName);
}

HRESULT VolumeReader::ParseBootSector(const CBinaryBuffer& buffer)
{
    if (FSVBR::FSType::UNKNOWN != m_fsType)
        return S_OK;

    HRESULT hr = E_FAIL;

    // save boot sector before parsing it
    m_BoostSector = buffer;

    // guess FS type
    m_fsType = FSVBR::GuessFSType(buffer);

    // ntfs
    if (FSVBR::FSType::NTFS == m_fsType)
    {
        PackedBootSector* pbs = reinterpret_cast<PackedBootSector*>(buffer.GetData());

        // does this look kinda like NTFS header?
        if ((pbs->PackedBpb.SectorsPerCluster != 0x01) && (pbs->PackedBpb.SectorsPerCluster != 0x02)
            && (pbs->PackedBpb.SectorsPerCluster != 0x04) && (pbs->PackedBpb.SectorsPerCluster != 0x08)
            && (pbs->PackedBpb.SectorsPerCluster != 0x10) && (pbs->PackedBpb.SectorsPerCluster != 0x20)
            && (pbs->PackedBpb.SectorsPerCluster != 0x40) && (pbs->PackedBpb.SectorsPerCluster != 0x80))
        {
            // sectors per cluster isn't a supported value
            Log::Error("NTFS: SectorsPerCluster is not a supported value");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        else if (
            pbs->PackedBpb.ReservedSectors != 0 || pbs->PackedBpb.Fats != 0 || pbs->PackedBpb.RootEntries != 0
            || pbs->PackedBpb.Sectors != 0 || pbs->PackedBpb.SectorsPerFat != 0 || pbs->PackedBpb.LargeSectors != 0)
        {
            // one of the fields that should be null are not null
            Log::Error("NTFS: fields that should be zero is not zero");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        m_BytesPerSector = pbs->PackedBpb.BytesPerSector;
        m_BytesPerCluster = (ULONG)pbs->PackedBpb.SectorsPerCluster * m_BytesPerSector;

        if (pbs->ClustersPerFileRecordSegment < 0)
        {
            m_BytesPerFRS = 1 << (-pbs->ClustersPerFileRecordSegment);
        }
        else
        {
            m_BytesPerFRS = m_BytesPerCluster * (ULONG)pbs->ClustersPerFileRecordSegment;
        }

        if (m_BytesPerFRS % 1024 != 0)
        {
            Log::Error("NTFS: Bytes per File Record is not a multiple of 1024 - {:#x}", m_BytesPerFRS);
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        m_NumberOfSectors = pbs->NumberSectors;
        m_llVolumeSerialNumber = pbs->SerialNumber;
    }
    // FAT12/FAT16/FAT32
    else if (FSVBR::FSType::FAT12 == m_fsType || FSVBR::FSType::FAT16 == m_fsType || FSVBR::FSType::FAT32 == m_fsType)
    {
        PPackedGenFatBootSector pbs = reinterpret_cast<PPackedGenFatBootSector>(buffer.GetData());

        m_BytesPerSector = pbs->PackedBpb.BytesPerSector;
        m_BytesPerCluster = (ULONG)pbs->PackedBpb.SectorsPerCluster * m_BytesPerSector;

        if (pbs->PackedBpb.Sectors == 0)
        {
            m_NumberOfSectors = pbs->PackedBpb.LargeSectors;
        }
        else
        {
            m_NumberOfSectors = pbs->PackedBpb.Sectors;
        }

        if (FSVBR::FSType::FAT12 == m_fsType || FSVBR::FSType::FAT16 == m_fsType)
        {
            PPackedFat1216BootSector fat1216bs = reinterpret_cast<PPackedFat1216BootSector>(buffer.GetData());
            m_llVolumeSerialNumber = fat1216bs->VolumeSerialNumber;
        }
        else
        {
            PPackedFat32BootSector fat32bs = reinterpret_cast<PPackedFat32BootSector>(buffer.GetData());
            m_llVolumeSerialNumber = fat32bs->VolumeSerialNumber;
        }
    }
    // REFS
    else if (FSVBR::FSType::REFS == m_fsType)
    {
        PackedRefsV1BootSector* pbs = reinterpret_cast<PackedRefsV1BootSector*>(buffer.GetData());

        m_NumberOfSectors = pbs->NumberSectors;
        m_BytesPerSector = pbs->SectorSize;
        m_BytesPerCluster = pbs->ClusterSize * m_BytesPerSector;
        m_llVolumeSerialNumber = 0;
    }
    else
    {
        return hr;
    }

    return S_OK;
}
