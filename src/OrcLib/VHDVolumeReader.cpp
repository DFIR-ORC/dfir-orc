//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "VHDVolumeReader.h"
#include "FileStream.h"

#include <intrin.h>

using namespace Orc;

VHDVolumeReader::VHDVolumeReader(const WCHAR* szLocation)
    : CompleteVolumeReader(szLocation)
{
}

HRESULT VHDVolumeReader::LoadDiskFooter()
{
    HRESULT hr = E_FAIL;

    FileStream stream;

    if (FAILED(hr = stream.ReadFrom(m_szLocation)))
    {
        Log::Error(L"Failed to open location '{}' [{}]", m_szLocation, SystemError(hr));
        return hr;
    }
    ULONGLONG ullNewPostion = 0LL;
    if (FAILED(hr = stream.SetFilePointer(-(LONGLONG)sizeof(Footer), FILE_END, &ullNewPostion)))
    {
        Log::Error(L"Failed to move to VHD's footer '{}' [{}]", m_szLocation, SystemError(hr));
        return hr;
    }
    ULONGLONG ullRead = 0L;
    if (FAILED(hr = stream.Read(&m_Footer, sizeof(Footer), &ullRead)))
    {
        Log::Error(L"Failed to read VHD's footer '{}' [{}]", m_szLocation, SystemError(hr));
        return hr;
    }
    m_Footer.Features = _byteswap_ulong(m_Footer.Features);
    m_Footer.Version = _byteswap_ulong(m_Footer.Version);
    m_Footer.DataOffset = _byteswap_uint64(m_Footer.DataOffset);
    m_Footer.TimeStamp = _byteswap_ulong(m_Footer.TimeStamp);
    m_Footer.CreatorVersion = _byteswap_ulong(m_Footer.CreatorVersion);
    m_Footer.OriginalSize = _byteswap_uint64(m_Footer.OriginalSize);
    m_Footer.CurrentSize = _byteswap_uint64(m_Footer.CurrentSize);
    m_Footer.Geometry.Cylinders = _byteswap_ushort(m_Footer.Geometry.Cylinders);
    m_Footer.DiskType = static_cast<DiskType>(_byteswap_ulong(m_Footer.DiskType));
    m_Footer.Checksum = _byteswap_ulong(m_Footer.Checksum);

    stream.Close();

    return S_OK;
}

HRESULT VHDVolumeReader::LoadDiskProperties()
{
    return LoadDiskFooter();
}

VHDVolumeReader::DiskType VHDVolumeReader::GetDiskType()
{
    if (!strncmp(m_Footer.Cookie, "conectix", 8))
    {
        return m_Footer.DiskType;
    }
    return VHDVolumeReader::None;
}

std::shared_ptr<VolumeReader> VHDVolumeReader::GetVolumeReader()
{
    HRESULT hr = E_FAIL;
    switch (GetDiskType())
    {
        case FixedHardDisk: {
            auto retval = std::make_shared<FixedVHDVolumeReader>(m_szLocation);

            if (FAILED(hr = retval->LoadDiskProperties()))
            {
                Log::Error(L"Failed to load VHD properties [{}]", SystemError(hr));
                return nullptr;
            }
            return retval;
        }
        default:
            return nullptr;
    }
}

VHDVolumeReader::~VHDVolumeReader(void) {}

std::shared_ptr<VolumeReader> FixedVHDVolumeReader::DuplicateReader()
{
    auto retval = std::make_shared<FixedVHDVolumeReader>(m_szLocation);

    retval->m_Footer = m_Footer;

    return retval;
}

HRESULT FixedVHDVolumeReader::LoadDiskProperties()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadDiskFooter()))
    {
        Log::Error(L"Failed to load VHD disk footer [{}]", SystemError(hr));
        return hr;
    }

    CDiskExtent extent(m_szLocation);

    if (FAILED(hr = extent.Open((FILE_SHARE_READ | FILE_SHARE_WRITE), OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        Log::Error(L"Failed to open vhd: '{}' [{}]", m_szLocation, SystemError(hr));
        return hr;
    }

    extent.m_Start = 0L;
    extent.m_Length = m_Footer.CurrentSize;

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
