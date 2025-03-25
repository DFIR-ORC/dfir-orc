//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <WinIoCtl.h>
#include <initguid.h>
#include <diskguid.h>

#include "PhysicalDiskReader.h"
#include "PartitionTable.h"

#include <regex>
#include <boost/scope_exit.hpp>

using namespace Orc;

static const auto MAX_PARTITION_TABLE = 16;

PhysicalDiskReader::PhysicalDiskReader(const WCHAR* szLocation)
    : CompleteVolumeReader(szLocation)
{
    ZeroMemory(m_szPhysDrive, ORC_MAX_PATH * sizeof(WCHAR));
    ZeroMemory(m_szDiskGUID, ORC_MAX_PATH * sizeof(WCHAR));
}

HRESULT PhysicalDiskReader::LoadDiskProperties(void)
{
    HRESULT hr = E_FAIL;

    if (IsReady())
        return S_OK;

    std::wregex physical_regex(REGEX_PHYSICALDRIVE, std::regex_constants::icase);
    std::wregex disk_regex(REGEX_DISK, std::regex_constants::icase);

    std::wstring location(m_szLocation);
    std::wsmatch m;

    try
    {

        if (std::regex_match(location, m, physical_regex))
        {
            if (m[REGEX_PHYSICALDRIVE_NUM].matched)
                m_uiDiskNum = (UINT)std::stoul(m[REGEX_PHYSICALDRIVE_NUM].str());
            else
            {
                Log::Error("Invalid physical drive number specification");
            }

            if (m[REGEX_PHYSICALDRIVE_PARTITION_NUM].matched)
                m_uiPartNum = (UINT)std::stoul(m[REGEX_PHYSICALDRIVE_PARTITION_NUM].str());
            else if (m[REGEX_PHYSICALDRIVE_OFFSET].matched)
            {
                m_ullOffset = (ULONGLONG)std::stoull(m[REGEX_PHYSICALDRIVE_OFFSET].str());

                if (m[REGEX_PHYSICALDRIVE_SIZE].matched)
                {
                    m_ullLength = (ULONGLONG)std::stoull(m[REGEX_PHYSICALDRIVE_SIZE].str());
                }
                if (m[REGEX_PHYSICALDRIVE_SECTOR].matched)
                {
                    m_uiSectorSize = (UINT)std::stoul(m[REGEX_PHYSICALDRIVE_SECTOR].str());
                }
            }
            else
            {
                m_uiPartNum = (UINT)-1;  // auto select active part
            }

            swprintf_s(m_szPhysDrive, L"\\\\.\\PhysicalDrive%d", m_uiDiskNum);
        }
        else if (std::regex_match(location, m, disk_regex))
        {
            if (m[REGEX_DISK_GUID].matched)
            {
                swprintf_s(m_szPhysDrive, L"\\\\.\\Disk%s", m[REGEX_DISK_GUID].str().c_str());
            }

            if (m[REGEX_DISK_PARTITION_NUM].matched)
                m_uiPartNum = (UINT)std::stoul(m[REGEX_DISK_PARTITION_NUM].str());

            else if (m[REGEX_DISK_OFFSET].matched)
            {
                m_ullOffset = (ULONGLONG)std::stoull(m[REGEX_DISK_OFFSET].str());

                if (m[REGEX_DISK_SIZE].matched)
                {
                    m_ullLength = (ULONGLONG)std::stoull(m[REGEX_DISK_SIZE].str());
                }
                if (m[REGEX_DISK_SECTOR].matched)
                {
                    m_uiSectorSize = (UINT)std::stoul(m[REGEX_DISK_SECTOR].str());
                }
            }
            else
            {
                m_uiPartNum = (UINT)-1;  // auto select active part
            }
        }
        else
        {
            Log::Error(L"Invalid physical drive reference: '{}'", m_szLocation);
            return E_INVALIDARG;
        }
    }
    catch (std::out_of_range)
    {
        Log::Error(L"Invalid physical drive reference: '{}'", m_szLocation);
        return E_INVALIDARG;
    }

    if (m_ullOffset == (ULONGLONG)-1)  // If no offset has been specified, we need to lookup the partition we are
                                       // interested in (-1 -> active part, other -> use part num)
    {
        PartitionTable pt;

        if (FAILED(hr = pt.LoadPartitionTable(m_szPhysDrive)))
        {
            Log::Error(L"Failed to load partition table for {} [{}]", m_szPhysDrive, SystemError(hr));
            return hr;
        }

        std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
            if (m_uiPartNum == (UINT)-1)
            {
                if (part.IsBootable())
                {
                    m_uiPartNum = part.PartitionNumber;

                    if (m_ullOffset == (ULONGLONG)-1)
                    {
                        m_ullOffset = part.Start;
                        m_ullLength = part.Size;
                    }
                }
            }
            else if (m_uiPartNum == part.PartitionNumber)
            {
                if (m_ullOffset == (ULONGLONG)-1)
                {
                    m_ullOffset = part.Start;
                    m_ullLength = part.Size;
                }
            }
        });

        if (m_uiPartNum == (UINT)-1)
        {
            Log::Error(L"Failed to determine partition to parse for \\\\.\\PhysicalDrive{}", m_uiDiskNum);
            return E_FAIL;
        }
    }

    CDiskExtent Extent(m_szPhysDrive, m_ullOffset, m_ullLength, m_BytesPerSector);
    if (FAILED(hr = Extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        Log::Error(L"Failed to open the drive: '{}' [{}]", m_szPhysDrive, SystemError(hr));
        return hr;
    }

    m_Extents.push_back(std::move(Extent));

    if (FAILED(hr = ParseBootSector()))
        return hr;

    // Update Extents with sector size
    auto bps = m_BytesPerSector;
    std::for_each(
        std::begin(m_Extents), std::end(m_Extents), [bps](CDiskExtent& extent) { extent.m_LogicalSectorSize = bps; });

    if (SUCCEEDED(hr))
        m_bReadyForEnumeration = true;

    return S_OK;
}

std::shared_ptr<VolumeReader> PhysicalDiskReader::DuplicateReader()
{
    auto retval = std::make_shared<PhysicalDiskReader>(m_szLocation);

    retval->m_uiDiskNum = m_uiDiskNum;
    retval->m_uiPartNum = m_uiPartNum;

    retval->m_ullOffset = m_ullOffset;
    retval->m_ullLength = m_ullLength;
    retval->m_uiSectorSize = m_uiSectorSize;

    wcscpy_s(retval->m_szPhysDrive, m_szPhysDrive);
    wcscpy_s(retval->m_szDiskGUID, m_szDiskGUID);

    return retval;
}

PhysicalDiskReader::~PhysicalDiskReader(void) {}
