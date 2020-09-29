//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "SystemStorageReader.h"

#include <regex>

#include "SystemStorageReader.h"

#include "ParameterCheck.h"

#include "PartitionTable.h"

using namespace std;

using namespace Orc;

std::shared_ptr<VolumeReader> SystemStorageReader::DuplicateReader()
{
    auto retval = std::make_shared<SystemStorageReader>(m_szLocation);

    retval->m_uiPartNum = m_uiPartNum;

    retval->m_ullOffset = m_ullOffset;
    retval->m_ullLength = m_ullLength;
    retval->m_uiSectorSize = m_uiSectorSize;

    return retval;
}

HRESULT SystemStorageReader::LoadDiskProperties(void)
{
    HRESULT hr = E_FAIL;

    if (IsReady())
        return S_OK;

    wregex storage_regex(REGEX_SYSTEMSTORAGE, std::regex_constants::icase);
    wsmatch m;

    wstring location(m_szLocation);

    if (!regex_match(location, m, storage_regex))
    {
        Log::Error(L"'{}' does not match a valid image file name", location);
        return E_INVALIDARG;
    }

    wstring strImageFile;
    if (m[REGEX_SYSTEMSTORAGE_SPEC].matched)
    {
        strImageFile = m[REGEX_SYSTEMSTORAGE_SPEC].str();
    }

    CDiskExtent extent(strImageFile);

    if (m[REGEX_SYSTEMSTORAGE_PARTITION_SPEC].matched)
    {
        PartitionTable pt;
        if (FAILED(hr = pt.LoadPartitionTable(strImageFile.c_str())))
        {
            Log::Error(L"Failed to load partition table for '{}' (code: {:#x})", strImageFile, hr);
            return hr;
        }

        Partition partition;

        if (m[REGEX_SYSTEMSTORAGE_PARTITION_NUM].matched)
        {
            UINT uiPartNum = std::stoi(m[REGEX_SYSTEMSTORAGE_PARTITION_NUM].str());
            std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                if (part.PartitionNumber == uiPartNum)
                    partition = part;
            });
        }
        else
        {
            std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                if (part.IsBootable())
                    partition = part;
            });
        }

        if (partition.PartitionNumber != 0)
        {
            // Here we go :-)
            extent.m_Start = partition.Start;
            extent.m_LogicalSectorSize = extent.m_PhysicalSectorSize = partition.SectorSize;
            extent.m_Length = partition.Size;
        }
    }
    else if (
        m[REGEX_SYSTEMSTORAGE_OFFSET].matched || m[REGEX_SYSTEMSTORAGE_SIZE].matched
        || m[REGEX_SYSTEMSTORAGE_SECTOR].matched)
    {
        // this "must" be a volume and NOT a full disk image
        LARGE_INTEGER offset = {0}, size = {0}, sector = {0};

        if (m[REGEX_SYSTEMSTORAGE_OFFSET].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_SYSTEMSTORAGE_OFFSET].str().c_str(), offset)))
            {
                Log::Error(L"Invalid offset specified: {} (code: {:#x})", m[REGEX_SYSTEMSTORAGE_OFFSET].str(), hr);
                return hr;
            }
        }

        if (m[REGEX_SYSTEMSTORAGE_SIZE].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_SYSTEMSTORAGE_SIZE].str().c_str(), size)))
            {
                Log::Error(L"Invalid size specified: {} (code: {:#x})", m[REGEX_SYSTEMSTORAGE_SIZE].str(), hr);
                return hr;
            }
        }

        if (m[REGEX_SYSTEMSTORAGE_SECTOR].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_SYSTEMSTORAGE_SECTOR].str().c_str(), sector)))
            {
                Log::Error(
                    L"Invalid sector size specified: '{}' (code: {:#x})", m[REGEX_SYSTEMSTORAGE_SECTOR].str(), hr);
                return hr;
            }
        }

        extent.m_Start = offset.QuadPart;
        extent.m_Length = size.QuadPart;
        extent.m_LogicalSectorSize = extent.m_PhysicalSectorSize = sector.LowPart;
    }

    if (FAILED(hr = extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        Log::Error(L"Failed to open image: '{}' (code: {:#x})", strImageFile, hr);
        return hr;
    }

    m_Extents.push_back(std::move(extent));

    if (FAILED(hr = ParseBootSector()))
        return hr;

    // Update Extents with sector size
    auto bps = m_BytesPerSector;
    ULONGLONG length = m_NumberOfSectors * m_BytesPerSector;

    std::for_each(std::begin(m_Extents), std::end(m_Extents), [bps, length](CDiskExtent& extent) {
        extent.m_LogicalSectorSize = extent.m_PhysicalSectorSize = bps;
        extent.m_Length = length;
    });

    if (SUCCEEDED(hr))
        m_bReadyForEnumeration = true;

    return S_OK;
}

SystemStorageReader::~SystemStorageReader(void) {}
