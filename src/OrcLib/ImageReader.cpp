//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <regex>

#include "ImageReader.h"

#include "ParameterCheck.h"

#include "PartitionTable.h"

using namespace std;

using namespace Orc;

#include <spdlog/spdlog.h>

ImageReader::ImageReader(const WCHAR* szImageFile)
    : CompleteVolumeReader(szImageFile)
{
    wcscpy_s(m_szImageReader, MAX_PATH, szImageFile);
}

HRESULT ImageReader::LoadDiskProperties(void)
{
    HRESULT hr = E_FAIL;

    if (IsReady())
        return S_OK;

    wregex image_regex(REGEX_IMAGE, std::regex_constants::icase);
    wsmatch m;

    wstring location(m_szImageReader);

    if (!regex_match(location, m, image_regex))
    {
        spdlog::error(L"'{}' does not match a valid image file name", location);
        return E_INVALIDARG;
    }

    wstring strImageFile;
    if (m[REGEX_IMAGE_SPEC].matched)
    {
        strImageFile = m[REGEX_IMAGE_SPEC].str();
    }

    CDiskExtent extent(strImageFile);

    if (m[REGEX_IMAGE_PARTITION_SPEC].matched)
    {
        PartitionTable pt;
        if (FAILED(hr = pt.LoadPartitionTable(strImageFile.c_str())))
        {
            spdlog::error(L"Failed to load partition table for '{}' (code: {:#x})", strImageFile, hr);
            return hr;
        }

        Partition partition;

        if (m[REGEX_IMAGE_PARTITION_NUM].matched)
        {
            UINT uiPartNum = std::stoi(m[REGEX_IMAGE_PARTITION_NUM].str());
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
            extent = CDiskExtent(strImageFile, partition.Start, partition.Size, partition.SectorSize);
        }
    }
    else if (m[REGEX_IMAGE_OFFSET].matched || m[REGEX_IMAGE_SIZE].matched || m[REGEX_IMAGE_SECTOR].matched)
    {
        // this "must" be a volume and NOT a full disk image
        LARGE_INTEGER offset = {0}, size = {0}, sector = {0};

        if (m[REGEX_IMAGE_OFFSET].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_OFFSET].str().c_str(), offset)))
            {
                spdlog::error(L"Invalid offset specified: {} (code: {:#x})", m[REGEX_IMAGE_OFFSET].str(), hr);
                return hr;
            }
        }

        if (m[REGEX_IMAGE_SIZE].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_SIZE].str().c_str(), size)))
            {
                spdlog::error(L"Invalid size specified: {} (code: {:#x})", m[REGEX_IMAGE_SIZE].str(), hr);
                return hr;
            }
        }

        if (m[REGEX_IMAGE_SECTOR].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_SECTOR].str().c_str(), sector)))
            {
                spdlog::error(L"Invalid sector size specified: '{}' (code: {:#x})", m[REGEX_IMAGE_SECTOR].str(), hr);
                return hr;
            }
        }

        extent.m_Start = offset.QuadPart;
        extent.m_Length = size.QuadPart;
        extent.m_LogicalSectorSize = extent.m_PhysicalSectorSize = sector.LowPart;
    }

    if (FAILED(hr = extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        spdlog::error(L"Failed to open image '{}' (code: {:#x})", strImageFile, hr);
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

std::shared_ptr<VolumeReader> ImageReader::DuplicateReader()
{
    return std::make_shared<ImageReader>(m_szImageReader);
}

ImageReader::~ImageReader(void) {}
