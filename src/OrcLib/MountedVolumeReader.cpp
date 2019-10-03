//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <regex>

#include "NtfsDataStructures.h"
#include "MountedVolumeReader.h"

#include "SystemDetails.h"

#include "DiskExtent.h"

#include "LogFileWriter.h"

#include "winioctl.h"

using namespace Orc;

MountedVolumeReader::MountedVolumeReader(logger pLog, const WCHAR* szVolumeName)
    : CompleteVolumeReader(std::move(pLog), szVolumeName)
{
    m_Name = L'\0';

    m_dwMaxComponentLength = 255;
    m_hDevice = INVALID_HANDLE_VALUE;

    wcscpy_s(m_VolumePath, szVolumeName);
    ZeroMemory(m_szShortVolumeName, ARRAYSIZE(m_szShortVolumeName) * sizeof(WCHAR));
    ZeroMemory(m_szVolumeName, ARRAYSIZE(m_szVolumeName) * sizeof(WCHAR));
}

//
// determine physical disk extents of logical disk
//
HRESULT MountedVolumeReader::GetDiskExtents()
{
    HRESULT hr = E_FAIL;

    CBinaryBuffer Buffer;

    do
    {

        //
        // logical extent
        //
        DISK_GEOMETRY_EX DiskGeometryEx = {{0}};
        GET_LENGTH_INFORMATION DiskLength = {{0}};

        CDiskExtent Extent(_L_, m_szVolumeName);
        DWORD dwSize;
        if (FAILED(
                hr = Extent.Open(
                    (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN)))
        {
            log::Error((_L_), hr, L"Failed to open the drive, so bailing...\r\n");
            break;
        }
        if (!::DeviceIoControl(
                Extent.m_hFile,
                IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                NULL,
                0,
                &DiskGeometryEx,
                sizeof(DiskGeometryEx),
                &dwSize,
                NULL))
        {
            log::Error(
                (_L_),
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to retrieve Disk Geometry using IOCTL_DISK_GET_DRIVE_GEOMETRY_EX.... retrying with "
                L"IOCTL_DISK_GET_DRIVE_GEOMETRY\r\n",
                GetLastError());

            DISK_GEOMETRY DiskGeometry = {{0}};

            if (!::DeviceIoControl(
                    Extent.m_hFile,
                    IOCTL_DISK_GET_DRIVE_GEOMETRY,
                    NULL,
                    0,
                    &DiskGeometry,
                    sizeof(DiskGeometry),
                    &dwSize,
                    NULL))
            {
                log::Error(
                    (_L_),
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to retrieve Disk Geometry using IOCTL_DISK_GET_DRIVE_GEOMETRY defaulting to \"safe\" "
                    L"value: 512 bytes per sector\r\n",
                    GetLastError());
                ZeroMemory(&DiskGeometryEx, sizeof(DiskGeometryEx));
                DiskGeometry.BytesPerSector = 512;
            }
            // Retry was successfull
            ZeroMemory(&DiskGeometryEx, sizeof(DiskGeometryEx));
            DiskGeometryEx.Geometry = DiskGeometry;
        }
        if (!::DeviceIoControl(
                Extent.m_hFile, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &DiskLength, sizeof(DiskLength), &dwSize, NULL))
        {
            // on Win2K, this information may not be available
            PARTITION_INFORMATION PartitionInfo;

            if (!::DeviceIoControl(
                    Extent.m_hFile,
                    IOCTL_DISK_GET_PARTITION_INFO,
                    NULL,
                    0,
                    &PartitionInfo,
                    sizeof(PartitionInfo),
                    &dwSize,
                    NULL))
            {
                log::Error(
                    (_L_), hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to retrieve Disk Length Info...\r\n");
                break;
            }
            DiskLength.Length = PartitionInfo.PartitionLength;
        }

        DWORD dwMajor = 0L, dwMinor = 0L;

        if (FAILED(hr = SystemDetails::GetOSVersion(dwMajor, dwMinor)))
        {
            log::Error((_L_), hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to retrieve OS version info...\r\n");
            break;
        }

        if (dwMajor >= 6)
        {
            STORAGE_PROPERTY_QUERY Query;
            Query.QueryType = PropertyStandardQuery;
            Query.PropertyId = StorageAccessAlignmentProperty;

            STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR saad;
            ZeroMemory(&saad, sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));
            DWORD dwBytesReturned = 0L;
            if (!DeviceIoControl(
                    Extent.m_hFile,
                    IOCTL_STORAGE_QUERY_PROPERTY,
                    &Query,
                    sizeof(STORAGE_PROPERTY_QUERY),
                    &saad,
                    sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR),
                    &dwBytesReturned,
                    NULL))
            {
                if (GetLastError() == ERROR_NOT_SUPPORTED)
                {
                    // at least VMWare does not support this IOCTL, we default to DiskGeometry.Geometry.BytesPerSector
                    Extent.m_PhysicalSectorSize = Extent.m_LogicalSectorSize = DiskGeometryEx.Geometry.BytesPerSector;
                }
				else if (GetLastError() == ERROR_INVALID_FUNCTION)
				{
                    log::Warning(
                        _L_,
                        hr = HRESULT_FROM_WIN32(GetLastError()),
                        L"IOCTL_STORAGE_QUERY_PROPERTY does not support StorageAccessAlignmentProperty for this device \"%s\"\r\n", Extent.GetName().c_str());
                    Extent.m_PhysicalSectorSize = Extent.m_LogicalSectorSize = DiskGeometryEx.Geometry.BytesPerSector;
				}
				else
				{
                    log::Error(
                        _L_,
                        hr = HRESULT_FROM_WIN32(GetLastError()),
                        L"Failed to retrieve Disk alignment Info ... (IOCTL_STORAGE_QUERY_PROPERTY)\r\n");
                    Extent.m_PhysicalSectorSize = Extent.m_LogicalSectorSize = DiskGeometryEx.Geometry.BytesPerSector;
                }
            }
            else
            {
                // IOCTL_STORAGE_QUERY_PROPERTY succeeded!!!!
                Extent.m_LogicalSectorSize = saad.BytesPerLogicalSector;
                Extent.m_PhysicalSectorSize = saad.BytesPerPhysicalSector;
            }
        }
        else
        {
            Extent.m_PhysicalSectorSize = Extent.m_LogicalSectorSize = DiskGeometryEx.Geometry.BytesPerSector;
        }
        Extent.m_Start = 0;
        Extent.m_Length = DiskLength.Length.QuadPart;

        m_Extents.push_back(std::move(Extent));
        hr = S_OK;
    } while (false);

    return hr;
}

HRESULT MountedVolumeReader::LoadDiskProperties(void)
{
    HRESULT hr = E_FAIL;

    if (IsReady())
        return S_OK;

    std::wregex r1(REGEX_MOUNTED_DRIVE);
    std::wregex r2(REGEX_MOUNTED_VOLUME, std::regex_constants::icase);
    std::wsmatch m;
    std::wstring s(m_VolumePath);

    if (std::regex_match(s, m, r1) && m[REGEX_MOUNTED_DRIVE_LETTER].matched)
    {
        m_Name = *m[REGEX_MOUNTED_DRIVE_LETTER].first;

        wcscpy_s(m_szVolumeName, MAX_VOLUME_NAME, L"\\\\.\\?:");
        m_szVolumeName[4] = m_Name;

        wcscpy_s(m_szShortVolumeName, MAX_VOLUME_NAME, L"?:\\");
        m_szShortVolumeName[0] = m_Name;
    }
    else if (std::regex_match(s, m, r2))
    {
        DWORD dwPathLen = 0;
        WCHAR szVolumePath[MAX_PATH];

        wcscpy_s(szVolumePath, m_VolumePath);
        wcscat_s(szVolumePath, L"\\");

        if (!GetVolumePathNamesForVolumeName(szVolumePath, NULL, 0L, &dwPathLen))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());

            if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
            {
                log::Error(_L_, hr, L"GetVolumePathNamesForVolumeName failed for volume %s\r\n", m_VolumePath);
                return hr;
            }
        }
        WCHAR* szPathNames = (WCHAR*)HeapAlloc(GetProcessHeap(), 0L, dwPathLen * sizeof(TCHAR));
        if (szPathNames == nullptr)
            return E_OUTOFMEMORY;

        if (!GetVolumePathNamesForVolumeName(szVolumePath, szPathNames, dwPathLen, &dwPathLen))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());

            if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
            {
                log::Error(_L_, hr, L"GetVolumePathNamesForVolumeName failed for volume %s\r\n", m_VolumePath);
                HeapFree(GetProcessHeap(), 0L, szPathNames);
                return hr;
            }
        }
        wcscpy_s(m_szShortVolumeName, MAX_VOLUME_NAME, szPathNames);
        HeapFree(GetProcessHeap(), 0L, szPathNames);
        wcscpy_s(m_szVolumeName, MAX_VOLUME_NAME, m_VolumePath);
    }
    else
    {
        wcscpy_s(m_szVolumeName, MAX_VOLUME_NAME, m_VolumePath);
        wcscpy_s(m_szShortVolumeName, MAX_VOLUME_NAME, m_VolumePath);
        wcscat_s(m_szShortVolumeName, MAX_VOLUME_NAME, L"\\");
    }

    WCHAR szFSName[MAX_PATH];

    if (!GetVolumeInformation(m_szShortVolumeName, NULL, NULL, NULL, &m_dwMaxComponentLength, NULL, szFSName, MAX_PATH))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!FSVBR::IsFSSupported(std::wstring(szFSName)))
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_SYSTEM_LIMITATION);
    }

    // open a handle to the root of the drive.
    HANDLE hRoot = CreateFile(
        m_szShortVolumeName,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (hRoot == INVALID_HANDLE_VALUE)
    {
        log::Error(
            (_L_),
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to open volume root (%s)\r\n",
            m_szShortVolumeName);
        return hr;
    }

    BY_HANDLE_FILE_INFORMATION fiRootFile;
    if (!GetFileInformationByHandle(hRoot, &fiRootFile))
    {
        CloseHandle(hRoot);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    else
    {
        m_llVolumeSerialNumber = fiRootFile.dwVolumeSerialNumber;
    }
    CloseHandle(hRoot);

    m_hDevice = CreateFile(
        m_szVolumeName,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (m_hDevice == INVALID_HANDLE_VALUE)
    {
        log::Error((_L_), hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to open volume \"%s\"\r\n", m_szVolumeName);
        return hr;
    }

    //
    // Load the disk parameters
    //
    if (FAILED(hr = GetDiskExtents()))
    {
        log::Error((_L_), hr, L"Failed to get volume extents!\r\n");
        return hr;
    }

    if (FAILED(hr = ParseBootSector()))
        return hr;

    if (SUCCEEDED(hr))
        m_bReadyForEnumeration = true;

    return hr;
}

std::shared_ptr<VolumeReader> MountedVolumeReader::DuplicateReader()
{
    auto retval = std::make_shared<MountedVolumeReader>(_L_, m_szLocation);

    retval->m_Name = m_Name;
    wcscpy_s(retval->m_szVolumeName, MAX_VOLUME_NAME, m_szVolumeName);
    wcscpy_s(retval->m_szShortVolumeName, MAX_VOLUME_NAME, m_szShortVolumeName);
    wcscpy_s(retval->m_VolumePath, MAX_VOLUME_NAME, m_VolumePath);

    DuplicateHandle(
        GetCurrentProcess(), m_hDevice, GetCurrentProcess(), &retval->m_hDevice, 0L, FALSE, DUPLICATE_SAME_ACCESS);

    return retval;
}
