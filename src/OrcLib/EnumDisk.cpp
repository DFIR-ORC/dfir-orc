//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "EnumDisk.h"

#include "BinaryBuffer.h"

#include <setupapi.h>  // for SetupDiXxx functions.
#include <cfgmgr32.h>  // for SetupDiXxx functions.
#include <devguid.h>
#include <ntddscsi.h>

#include <algorithm>

#include <boost\scope_exit.hpp>
#include "Utils/WinApi.h"

#include "Log/Log.h"

using namespace Orc;

namespace {

bool IsPrintableAscii(uint8_t byte)
{
    // SCSI and ATA both define these fields as printable ASCII
    return byte >= 0x20 && byte <= 0x7E;
}

// The leading printable ASCII of 'bytes', without the padding spaces on its right. A device chooses these bytes, so
// anything outside printable ASCII ends the string rather than reaching an output column.
std::wstring ToTrimmedWide(const uint8_t* bytes, size_t count)
{
    size_t end = 0;
    while (end < count && IsPrintableAscii(bytes[end]))
    {
        ++end;
    }

    while (end > 0 && bytes[end - 1] == ' ')
    {
        --end;
    }

    std::wstring value;
    value.reserve(end);
    for (size_t i = 0; i < end; ++i)
    {
        value.push_back(static_cast<wchar_t>(bytes[i]));
    }

    return value;
}

// IOCTL_SCSI_PASS_THROUGH takes its buffers behind the structure, in the same allocation
struct SCSI_PASS_THROUGH_WITH_BUFFERS
{
    SCSI_PASS_THROUGH Spt;
    ULONG Filler;  // realign the buffers on a double word boundary
    UCHAR SenseBuf[32];
    UCHAR DataBuf[512];
};

const UCHAR kScsiOpAtaPassThrough12 = 0xA1;
const UCHAR kAtaCommandIdentifyDevice = 0xEC;
const size_t kAtaIdentifySerialFirstWord = 10;
const size_t kAtaIdentifySerialWordCount = 10;
const size_t kAtaIdentifyModelFirstWord = 27;
const size_t kAtaIdentifyModelWordCount = 20;

// What StorageDeviceProperty reports about the device behind 'device'. False when it did not answer, leaving every
// identity field of 'disk' as it was. IOCTL_STORAGE_QUERY_PROPERTY is FILE_ANY_ACCESS, so the read handle the
// enumeration already holds carries it and no elevation is involved.
bool QueryStorageIdentity(HANDLE device, EnumDisk::PhysicalDisk& disk)
{
    STORAGE_PROPERTY_QUERY query;
    ZeroMemory(&query, sizeof(query));
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    //
    // The header first: only the device knows how long its descriptor is
    //

    STORAGE_DESCRIPTOR_HEADER header;
    ZeroMemory(&header, sizeof(header));

    DWORD returned = 0L;
    if (!DeviceIoControl(
            device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &returned, NULL))
    {
        Log::Debug(
            "Failed IOCTL_STORAGE_QUERY_PROPERTY for the descriptor length [{}]",
            SystemError(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }

    if (returned < sizeof(header) || header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR))
    {
        Log::Debug("StorageDeviceProperty announced a descriptor of {} bytes", header.Size);
        return false;
    }

    CBinaryBuffer buffer;
    if (!buffer.SetCount(header.Size))
    {
        Log::Debug("Failed to allocate {} bytes for a storage device descriptor", header.Size);
        return false;
    }
    buffer.ZeroMe();

    returned = 0L;
    if (!DeviceIoControl(
            device,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            buffer.GetData(),
            static_cast<DWORD>(buffer.GetCount()),
            &returned,
            NULL))
    {
        Log::Debug(
            "Failed IOCTL_STORAGE_QUERY_PROPERTY for the descriptor [{}]",
            SystemError(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }

    if (returned < sizeof(STORAGE_DEVICE_DESCRIPTOR))
    {
        Log::Debug("StorageDeviceProperty returned {} bytes, short of a descriptor", returned);
        return false;
    }

    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.GetData());

    // The device chose both the offsets and the strings they point to: they are read inside what it actually
    // returned and not one byte further, whatever its descriptor claims
    const auto answer = BasicBufferSpan<const uint8_t>(
        buffer.GetData(), std::min<size_t>(returned, static_cast<size_t>(descriptor->Size)));

    disk.VendorId = EnumDisk::ParseDescriptorString(answer, descriptor->VendorIdOffset);
    disk.ProductId = EnumDisk::ParseDescriptorString(answer, descriptor->ProductIdOffset);
    disk.ProductRevision = EnumDisk::ParseDescriptorString(answer, descriptor->ProductRevisionOffset);
    disk.SerialNumber = EnumDisk::ParseDescriptorString(answer, descriptor->SerialNumberOffset);
    disk.BusType = descriptor->BusType;
    disk.RemovableMedia = descriptor->RemovableMedia != FALSE;

    Log::Debug(
        L"Identity of '{}': vendor '{}', product '{}', revision '{}', serial '{}', bus type {:#x}, removable {}",
        disk.InterfacePath,
        disk.VendorId,
        disk.ProductId,
        disk.ProductRevision,
        disk.SerialNumber,
        static_cast<uint32_t>(disk.BusType),
        disk.RemovableMedia);

    return true;
}

// Model and serial of the drive inside an enclosure, asked to the drive itself with an ATA IDENTIFY DEVICE carried
// by SAT. Leaves 'disk' untouched when the bridge does not implement SAT, which many do not.
void QueryEnclosedIdentity(const wchar_t* devicePath, EnumDisk::PhysicalDisk& disk)
{
    //
    // IOCTL_SCSI_PASS_THROUGH is declared FILE_READ_ACCESS | FILE_WRITE_ACCESS, so the read only handle the
    // enumeration opened cannot carry it: this takes one of its own and gives it back here
    //

    auto device = CreateFileApi(
        devicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (!device)
    {
        Log::Debug(L"Failed CreateFile for read and write on '{}', no SAT identity [{}]", devicePath, device.error());
        return;
    }

    SCSI_PASS_THROUGH_WITH_BUFFERS sptwb;
    ZeroMemory(&sptwb, sizeof(sptwb));

    sptwb.Spt.Length = sizeof(SCSI_PASS_THROUGH);
    sptwb.Spt.CdbLength = 12;
    sptwb.Spt.SenseInfoLength = sizeof(sptwb.SenseBuf);
    sptwb.Spt.DataIn = SCSI_IOCTL_DATA_IN;
    sptwb.Spt.DataTransferLength = sizeof(sptwb.DataBuf);
    sptwb.Spt.TimeOutValue = 5;
    sptwb.Spt.DataBufferOffset = offsetof(SCSI_PASS_THROUGH_WITH_BUFFERS, DataBuf);
    sptwb.Spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_WITH_BUFFERS, SenseBuf);

    // ATA PASS-THROUGH(12) carrying IDENTIFY DEVICE
    sptwb.Spt.Cdb[0] = kScsiOpAtaPassThrough12;
    sptwb.Spt.Cdb[1] = 4 << 1;  // PROTOCOL 4, PIO data in
    sptwb.Spt.Cdb[2] = 0x0E;  // T_DIR from the device, BYT_BLOK in blocks, T_LENGTH in the sector count
    sptwb.Spt.Cdb[4] = 1;  // one sector
    sptwb.Spt.Cdb[9] = kAtaCommandIdentifyDevice;

    DWORD returned = 0L;
    if (!DeviceIoControl(
            device->value(),
            IOCTL_SCSI_PASS_THROUGH,
            &sptwb,
            sizeof(SCSI_PASS_THROUGH),
            &sptwb,
            sizeof(sptwb),
            &returned,
            NULL))
    {
        Log::Debug(
            L"Failed IOCTL_SCSI_PASS_THROUGH ATA IDENTIFY on '{}' [{}]",
            devicePath,
            SystemError(HRESULT_FROM_WIN32(GetLastError())));
        return;
    }

    if (sptwb.Spt.ScsiStatus != 0)
    {
        // A bridge with no SAT answers CHECK CONDITION, its sense data saying ILLEGAL REQUEST
        Log::Debug(
            L"ATA IDENTIFY refused on '{}', SCSI status {:#x}, sense {:#x}/{:#x}/{:#x}",
            devicePath,
            sptwb.Spt.ScsiStatus,
            sptwb.SenseBuf[2] & 0x0F,
            sptwb.SenseBuf[12],
            sptwb.SenseBuf[13]);
        return;
    }

    const auto identify = BasicBufferSpan<const uint8_t>(sptwb.DataBuf, sizeof(sptwb.DataBuf));

    disk.EnclosedModel =
        EnumDisk::ParseAtaIdentifyString(identify, kAtaIdentifyModelFirstWord, kAtaIdentifyModelWordCount);
    disk.EnclosedSerial =
        EnumDisk::ParseAtaIdentifyString(identify, kAtaIdentifySerialFirstWord, kAtaIdentifySerialWordCount);

    Log::Debug(L"Enclosed drive of '{}': model '{}', serial '{}'", devicePath, disk.EnclosedModel, disk.EnclosedSerial);
}

}  // namespace

std::wstring EnumDisk::ParseDescriptorString(BasicBufferSpan<const uint8_t> descriptor, DWORD offset)
{
    const size_t size = static_cast<size_t>(descriptor.size());

    // 0 is how a STORAGE_DEVICE_DESCRIPTOR says the device reported no such field
    if (offset == 0 || offset >= size)
    {
        return {};
    }

    const uint8_t* bytes = descriptor.data();

    size_t end = offset;
    while (end < size && bytes[end] != 0x00)
    {
        ++end;
    }

    if (end == size)
    {
        // The storage stack terminates the strings its descriptor points to: an unterminated one is malformed
        return {};
    }

    return ToTrimmedWide(bytes + offset, end - offset);
}

std::wstring
EnumDisk::ParseAtaIdentifyString(BasicBufferSpan<const uint8_t> identify, size_t firstWord, size_t wordCount)
{
    const size_t words = static_cast<size_t>(identify.size()) / 2;

    // A device that answered short must not be read past, and the range itself must not overflow
    if (wordCount == 0 || firstWord > words || wordCount > words - firstWord)
    {
        return {};
    }

    const uint8_t* bytes = identify.data() + firstWord * 2;
    const size_t count = wordCount * 2;

    // ATA holds the earlier character of a pair in the high byte of its word
    std::vector<uint8_t> swapped(count);
    for (size_t i = 0; i + 1 < count; i += 2)
    {
        swapped[i] = bytes[i + 1];
        swapped[i + 1] = bytes[i];
    }

    return ToTrimmedWide(swapped.data(), swapped.size());
}

HRESULT EnumDisk::GetDevice(HDEVINFO hDevInfo, DWORD Index, PhysicalDisk& aDisk, const GUID guidDeviceClass)
{
    HRESULT hr = E_FAIL;

    SP_DEVICE_INTERFACE_DATA interfaceData;
    ZeroMemory(&interfaceData, sizeof(SP_DEVICE_INTERFACE_DATA));
    interfaceData.cbSize = sizeof(SP_INTERFACE_DEVICE_DATA);

    if (!SetupDiEnumDeviceInterfaces(hDevInfo, 0, (LPGUID)&guidDeviceClass, Index, &interfaceData))
    {
        if (GetLastError() == ERROR_NO_MORE_ITEMS)
        {
            Log::Debug("No more interfaces");
        }
        else
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed SetupDiEnumDeviceInterfaces [{}]", SystemError(hr));
            return hr;
        }
        return S_FALSE;
    }

    //
    // This call returns ERROR_INSUFFICIENT_BUFFER with reqSize
    // set to the required buffer size. Ignore the above error and
    // pass a bigger buffer to get the detail data
    //
    DWORD interfaceDetailDataSize = 0L;
    if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, NULL, 0, &interfaceDetailDataSize, NULL))
    {

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed SetupDiGetDeviceInterfaceDetail [{}]", SystemError(hr));
            return hr;
        }
    }

    //
    // Allocate memory to get the interface detail data
    // This contains the devicepath we need to open the device
    //

    PSP_DEVICE_INTERFACE_DETAIL_DATA interfaceDetailData =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(interfaceDetailDataSize);
    if (interfaceDetailData == NULL)
    {
        Log::Error("Unable to allocate memory to get the interface detail data");
        return E_OUTOFMEMORY;
    }
    interfaceDetailData->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
    if (!SetupDiGetDeviceInterfaceDetail(
            hDevInfo, &interfaceData, interfaceDetailData, interfaceDetailDataSize, &interfaceDetailDataSize, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed SetupDiGetDeviceInterfaceDetail [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT(&interfaceDetailData)
    {
        free(interfaceDetailData);
    }
    BOOST_SCOPE_EXIT_END;

    //
    // Now we have the device path. Open the device interface to check the caller will be able to read it
    //

    Log::Debug(L"Interface: {}", interfaceDetailData->DevicePath);

    aDisk.InterfacePath = interfaceDetailData->DevicePath;

    auto hDevice = CreateFileApi(
        interfaceDetailData->DevicePath,  // device interface name
        GENERIC_READ,  // dwDesiredAccess
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // dwShareMode
        NULL,  // lpSecurityAttributes
        OPEN_EXISTING,  // dwCreationDistribution
        0,  // dwFlagsAndAttributes
        NULL  // hTemplateFile
    );

    if (!hDevice)
    {
        Log::Debug(L"Failed CreateFile for disk interface '{}' [{}]", interfaceDetailData->DevicePath, hDevice.error());
        return ToHRESULT(hDevice.error());
    }

    //
    // Identity comes on top of the interface path, which is what the enumeration is for: a device that will not
    // answer for its identity is still a disk to report, so nothing below can fail GetDevice()
    //

    const bool identified = QueryStorageIdentity(hDevice->value(), aDisk);

    // A bridge answers for itself and not for the drive it carries, so ask that drive directly. An identity that
    // did not come at all gets the same attempt, on the chance the device only refused the storage property path.
    if (!identified || aDisk.BusType == BusTypeUsb || aDisk.BusType == BusType1394)
    {
        QueryEnclosedIdentity(interfaceDetailData->DevicePath, aDisk);
    }

    return S_OK;
}

HRESULT EnumDisk::EnumerateDisks(std::vector<PhysicalDisk>& disks, const GUID guidDeviceClass)
{
    HRESULT hr = E_FAIL;

    //
    // Open the device using device interface registered by the driver
    //

    //
    // Get the interface device information set that contains all devices of event class.
    //

    HDEVINFO hDevInfo =
        SetupDiGetClassDevs((LPGUID)&guidDeviceClass, NULL, NULL, (DIGCF_PRESENT | DIGCF_INTERFACEDEVICE));

    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed SetupDiGetClassDevs [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hDevInfo))
    {
        if (hDevInfo != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(hDevInfo);
        hDevInfo = INVALID_HANDLE_VALUE;
    }
    BOOST_SCOPE_EXIT_END;

    //
    //  Enumerate all the disk devices
    //
    for (DWORD index = 0;; index++)
    {
        PhysicalDisk aDisk;

        hr = GetDevice(hDevInfo, index, aDisk, guidDeviceClass);
        if (hr == S_FALSE)
            break;  // Enumeration done!

        if (FAILED(hr))
        {
            // One device the caller cannot use must not hide the disks enumerated after it
            Log::Debug("Skipping disk device interface #{} [{}]", index, SystemError(hr));
            continue;
        }

        disks.push_back(std::move(aDisk));
    }

    return S_OK;
}

EnumDisk::~EnumDisk() {}
