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

ImageReader::ImageReader(logger pLog, const WCHAR* szImageFile)
    : CompleteVolumeReader(std::move(pLog), szImageFile)
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
        log::Error(_L_, E_INVALIDARG, L"%s does not match a valid image file name\r\n", location.c_str());
        return E_INVALIDARG;
    }

    wstring strImageFile;
    if (m[REGEX_IMAGE_SPEC].matched)
    {
        strImageFile = m[REGEX_IMAGE_SPEC].str();
    }

    CDiskExtent extent(_L_, strImageFile);

    if (m[REGEX_IMAGE_PARTITION_SPEC].matched)
    {
        PartitionTable pt(_L_);
        if (FAILED(hr = pt.LoadPartitionTable(strImageFile.c_str())))
        {
            log::Error(_L_, hr, L"Failed to load partition table for %s\r\n", strImageFile.c_str());
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
            extent = CDiskExtent(_L_, strImageFile, partition.Start, partition.Size, partition.SectorSize);
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
                log::Error(_L_, hr, L"Invalid offset specified: %s\r\n", m[REGEX_IMAGE_OFFSET].str().c_str());
                return hr;
            }
        }

        if (m[REGEX_IMAGE_SIZE].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_SIZE].str().c_str(), size)))
            {
                log::Error(_L_, hr, L"Invalid size specified: %s\r\n", m[REGEX_IMAGE_SIZE].str().c_str());
                return hr;
            }
        }

        if (m[REGEX_IMAGE_SECTOR].matched)
        {
            if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_SECTOR].str().c_str(), sector)))
            {
                log::Error(_L_, hr, L"Invalid sector size specified: %s\r\n", m[REGEX_IMAGE_SECTOR].str().c_str());
                return hr;
            }
        }

        extent.m_Start = offset.QuadPart;
        extent.m_Length = size.QuadPart;
        extent.m_LogicalSectorSize = extent.m_PhysicalSectorSize = sector.LowPart;
    }

    if (FAILED(hr = extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        log::Error((_L_), hr, L"Failed to open image %s\r\n", strImageFile.c_str());
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
    return std::make_shared<ImageReader>(_L_, m_szImageReader);
}

ImageReader::~ImageReader(void) {}
