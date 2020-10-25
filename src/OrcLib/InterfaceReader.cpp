//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "InterfaceReader.h"

#include "PartitionTable.h"
#include "WideAnsi.h"

#include <regex>

#include <boost\scope_exit.hpp>

#include "Log/Log.h"

using namespace Orc;

HRESULT InterfaceReader::LoadDiskProperties()
{
    HRESULT hr = E_FAIL;

    if (IsReady())
        return S_OK;

    std::wregex physical_regex(REGEX_INTERFACE, std::regex_constants::icase);

    std::wstring location(m_szLocation);
    std::wsmatch m;

    try
    {

        if (std::regex_match(location, m, physical_regex))
        {
            if (m[REGEX_INTERFACE_PARTITION_NUM].matched)
                m_uiPartNum = (UINT)std::stoul(m[REGEX_INTERFACE_PARTITION_NUM].str());
            else if (m[REGEX_INTERFACE_OFFSET].matched)
            {
                m_ullOffset = (ULONGLONG)std::stoull(m[REGEX_INTERFACE_OFFSET].str());

                if (m[REGEX_INTERFACE_SIZE].matched)
                {
                    m_ullLength = (ULONGLONG)std::stoull(m[REGEX_INTERFACE_SIZE].str());
                }
                if (m[REGEX_INTERFACE_SECTOR].matched)
                {
                    m_uiSectorSize = (UINT)std::stoul(m[REGEX_INTERFACE_SECTOR].str());
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
    catch (const std::out_of_range& e)
    {
        auto [hr, msg] = AnsiToWide(e.what());
        Log::Error(L"Invalid physical drive reference: '{}' ({})", m_szLocation, msg);
        return E_INVALIDARG;
    }

    m_InterfacePath = m[REGEX_INTERFACE_SPEC].str();

    if (m_uiPartNum != (UINT)-1)
    {
        PartitionTable pt;

        if (FAILED(hr = pt.LoadPartitionTable(m_InterfacePath.c_str())))
        {
            Log::Error(L"Invalid partition table found on interface '{}' [{}]", m_InterfacePath, SystemError(hr));
            return hr;
        }

        for (auto& part : pt.Table())
        {
            if (part.PartitionNumber == m_uiPartNum)
            {
                CDiskExtent Extent(m_InterfacePath, part.Start, part.Size, part.SectorSize);

                if (FAILED(
                        hr = Extent.Open(
                            (FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
                {
                    Log::Error(L"Failed to open the interface '{}' [{}]", m_InterfacePath, SystemError(hr));
                    return hr;
                }

                m_Extents.push_back(std::move(Extent));
            }
        }
    }
    else if (m_ullOffset != (ULONGLONG)-1)
    {
        CDiskExtent Extent(m_InterfacePath.c_str(), m_ullOffset, m_ullLength, m_uiSectorSize);
        if (FAILED(hr = Extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
        {
            Log::Error(L"Failed to open the drive '{}' [{}]", m_InterfacePath, SystemError(hr));
            return hr;
        }

        m_Extents.push_back(std::move(Extent));
    }
    else
    {
        Log::Error(L"No partition number or offset specified for interface reader: '{}'", m_szLocation);
        return E_INVALIDARG;
    }

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

std::shared_ptr<VolumeReader> InterfaceReader::DuplicateReader()
{
    auto retval = std::make_shared<InterfaceReader>(m_szLocation);

    retval->m_uiPartNum = m_uiPartNum;

    retval->m_ullOffset = m_ullOffset;
    retval->m_ullLength = m_ullLength;
    retval->m_uiSectorSize = m_uiSectorSize;

    retval->m_InterfacePath = m_InterfacePath;

    return retval;
}

InterfaceReader::~InterfaceReader() {}
