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

#include "Log/Log.h"

using namespace Orc;

std::shared_ptr<VolumeReader> SnapshotVolumeReader::DuplicateReader()
{
    auto retval = std::make_shared<SnapshotVolumeReader>(m_Shadow);

    return retval;
}

SnapshotVolumeReader::SnapshotVolumeReader(const VolumeShadowCopies::Shadow& Snapshot)
    : CompleteVolumeReader(Snapshot.DeviceInstance.c_str())
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
        Log::Error(L"Invalid physical drive reference: '{}'", m_Shadow.DeviceInstance);
        return E_INVALIDARG;
    }

    CDiskExtent extent(m_Shadow.DeviceInstance.c_str());

    if (FAILED(hr = extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        Log::Error(L"Failed to open the drive '{}' [{}]", m_Shadow.DeviceInstance, SystemError(hr));
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

HRESULT
SnapshotVolumeReader::Read(ULONGLONG offset, CBinaryBuffer& buffer, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    HRESULT hr = E_FAIL;
    const size_t bytesPerCowBlock = 16384;

    ullBytesRead = 0;

    Log::Trace("VSS: read (offset: {:#016x}, length: {})", offset, ullBytesToRead);

    if (!buffer.CheckCount(ullBytesToRead))
    {
        return E_OUTOFMEMORY;
    }

    //
    // BEWARE
    //
    // Always zero the buffer ! As VirtualAlloc is being use it is not required here. But because volsnap.sys (shadow
    // copy) can return sucessfully without modifying the provided buffer when trying to read unused/free cow blocks, it
    // must be zeroed.
    //
    CBinaryBuffer localBuffer(true);
    localBuffer.SetCount(bytesPerCowBlock);

    //
    // Workaround: process the request as multiple block of 16384 bytes.
    // See https://github.com/DFIR-ORC/readshadow for more details.
    //
    if (offset % bytesPerCowBlock != 0)
    {
        const auto firstReadLength = std::min(ullBytesToRead, bytesPerCowBlock - (offset % bytesPerCowBlock));

        hr = CompleteVolumeReader::Read(offset, localBuffer, firstReadLength, ullBytesRead);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = localBuffer.CopyTo(buffer.GetData(), ullBytesRead);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    while (ullBytesRead < ullBytesToRead)
    {
        ULONGLONG processed;

        //
        // Workaround: ensure the buffer is initialized because underlying ReadFile may be successful without modifying
        // the buffer. See https://github.com/DFIR-ORC/readshadow for more details.
        //
        localBuffer.ZeroMe();

        // BEWARE: the Read can change the size of the buffer...
        hr = CompleteVolumeReader::Read(
            offset + ullBytesRead,
            localBuffer,
            std::min(ullBytesToRead - ullBytesRead, static_cast<ULONGLONG>(bytesPerCowBlock)),
            processed);

        if (FAILED(hr))
        {
            return hr;
        }

        if (processed == 0)
        {
            break;
        }

        hr = localBuffer.CopyTo(buffer.GetData() + ullBytesRead, std::min(buffer.GetCount() - ullBytesRead, processed));
        if (FAILED(hr))
        {
            return hr;
        }

        ullBytesRead += processed;
    }

    return S_OK;
}
