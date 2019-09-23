//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "SnapshotVolumeReader.h"

#include "DiskExtent.h"

#include <regex>

using namespace Orc;

std::shared_ptr<VolumeReader> SnapshotVolumeReader::DuplicateReader()
{
    auto retval = std::make_shared<SnapshotVolumeReader>(_L_, m_Shadow);

    return retval;
}

SnapshotVolumeReader::SnapshotVolumeReader(logger pLog, const VolumeShadowCopies::Shadow& Snapshot)
    : CompleteVolumeReader(std::move(pLog), Snapshot.DeviceInstance.c_str())
    , m_Shadow(Snapshot)
{
}

HRESULT SnapshotVolumeReader::LoadDiskProperties(void)
{
    HRESULT hr = E_FAIL;

    if (IsReady())
        return S_OK;

    std::wregex snapshot_regex(REGEX_SNAPSHOT, std::regex_constants::icase);

    std::wstring location(m_Shadow.DeviceInstance.c_str());
    std::wsmatch m;

    if (!std::regex_match(location, m, snapshot_regex))
    {
        log::Error(_L_, E_INVALIDARG, L"Invalid physical drive reference: %s\r\n", m_Shadow.DeviceInstance.c_str());
        return E_INVALIDARG;
    }

    CDiskExtent extent(_L_, m_Shadow.DeviceInstance.c_str());

    if (FAILED(hr = extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        log::Error((_L_), hr, L"Failed to open the drive, so bailing...\r\n");
        return hr;
    }

    m_Extents.push_back(std::move(extent));

    if (FAILED(hr = ParseBootSector()))
        return hr;

    // Update Extents with sector size
    auto bps = m_BytesPerSector;
    ULONGLONG length = m_NumberOfSectors * m_BytesPerSector;

    std::for_each(std::begin(m_Extents), std::end(m_Extents), [bps, length](CDiskExtent& extent) {
        extent.m_LogicalSectorSize = bps;
        extent.m_Start = 0;
        extent.m_Length = length;
    });

    if (SUCCEEDED(hr))
        m_bReadyForEnumeration = true;

    return S_OK;
}

SnapshotVolumeReader::~SnapshotVolumeReader(void) {}
