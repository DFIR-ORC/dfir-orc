//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "EnumDisk.h"

#include "BinaryBuffer.h"

#include <setupapi.h>  // for SetupDiXxx functions.
#include <cfgmgr32.h>  // for SetupDiXxx functions.
#include <ntddscsi.h>
#include <devguid.h>

#include <boost\scope_exit.hpp>

#include "Log/Log.h"

using namespace Orc;

struct SCSI_PASS_THROUGH_WITH_BUFFERS
{
    SCSI_PASS_THROUGH Spt;
    ULONG Filler;  // realign buffers to double word boundary
    UCHAR SenseBuf[32];
    UCHAR DataBuf[512];
};

using PSCSI_PASS_THROUGH_WITH_BUFFERS = SCSI_PASS_THROUGH_WITH_BUFFERS*;

//
// Command Descriptor Block constants.
//

static const auto CDB6GENERIC_LENGTH = 6;
static const auto CDB10GENERIC_LENGTH = 10;

//
// SCSI CDB operation codes
//

static const auto SCSIOP_TEST_UNIT_READY = 0x00;
static const auto SCSIOP_REZERO_UNIT = 0x01;
static const auto SCSIOP_REWIND = 0x01;
static const auto SCSIOP_REQUEST_BLOCK_ADDR = 0x02;
static const auto SCSIOP_REQUEST_SENSE = 0x03;
static const auto SCSIOP_FORMAT_UNIT = 0x04;
static const auto SCSIOP_READ_BLOCK_LIMITS = 0x05;
static const auto SCSIOP_REASSIGN_BLOCKS = 0x07;
static const auto SCSIOP_READ6 = 0x08;
static const auto SCSIOP_RECEIVE = 0x08;
static const auto SCSIOP_WRITE6 = 0x0A;
static const auto SCSIOP_PRINT = 0x0A;
static const auto SCSIOP_SEND = 0x0A;
static const auto SCSIOP_SEEK6 = 0x0B;
static const auto SCSIOP_TRACK_SELECT = 0x0B;
static const auto SCSIOP_SLEW_PRINT = 0x0B;
static const auto SCSIOP_SEEK_BLOCK = 0x0C;
static const auto SCSIOP_PARTITION = 0x0D;
static const auto SCSIOP_READ_REVERSE = 0x0F;
static const auto SCSIOP_WRITE_FILEMARKS = 0x10;
static const auto SCSIOP_FLUSH_BUFFER = 0x10;
static const auto SCSIOP_SPACE = 0x11;
static const auto SCSIOP_INQUIRY = 0x12;
static const auto SCSIOP_VERIFY6 = 0x13;
static const auto SCSIOP_RECOVER_BUF_DATA = 0x14;
static const auto SCSIOP_MODE_SELECT = 0x15;
static const auto SCSIOP_RESERVE_UNIT = 0x16;
static const auto SCSIOP_RELEASE_UNIT = 0x17;
static const auto SCSIOP_COPY = 0x18;
static const auto SCSIOP_ERASE = 0x19;
static const auto SCSIOP_MODE_SENSE = 0x1A;
static const auto SCSIOP_START_STOP_UNIT = 0x1B;
static const auto SCSIOP_STOP_PRINT = 0x1B;
static const auto SCSIOP_LOAD_UNLOAD = 0x1B;
static const auto SCSIOP_RECEIVE_DIAGNOSTIC = 0x1C;
static const auto SCSIOP_SEND_DIAGNOSTIC = 0x1D;
static const auto SCSIOP_MEDIUM_REMOVAL = 0x1E;
static const auto SCSIOP_READ_CAPACITY = 0x25;
static const auto SCSIOP_READ = 0x28;
static const auto SCSIOP_WRITE = 0x2A;
static const auto SCSIOP_SEEK = 0x2B;
static const auto SCSIOP_LOCATE = 0x2B;
static const auto SCSIOP_WRITE_VERIFY = 0x2E;
static const auto SCSIOP_VERIFY = 0x2F;
static const auto SCSIOP_SEARCH_DATA_HIGH = 0x30;
static const auto SCSIOP_SEARCH_DATA_EQUAL = 0x31;
static const auto SCSIOP_SEARCH_DATA_LOW = 0x32;
static const auto SCSIOP_SET_LIMITS = 0x33;
static const auto SCSIOP_READ_POSITION = 0x34;
static const auto SCSIOP_SYNCHRONIZE_CACHE = 0x35;
static const auto SCSIOP_COMPARE = 0x39;
static const auto SCSIOP_COPY_COMPARE = 0x3A;
static const auto SCSIOP_WRITE_DATA_BUFF = 0x3B;
static const auto SCSIOP_READ_DATA_BUFF = 0x3C;
static const auto SCSIOP_CHANGE_DEFINITION = 0x40;
static const auto SCSIOP_READ_SUB_CHANNEL = 0x42;
static const auto SCSIOP_READ_TOC = 0x43;
static const auto SCSIOP_READ_HEADER = 0x44;
static const auto SCSIOP_PLAY_AUDIO = 0x45;
static const auto SCSIOP_PLAY_AUDIO_MSF = 0x47;
static const auto SCSIOP_PLAY_TRACK_INDEX = 0x48;
static const auto SCSIOP_PLAY_TRACK_RELATIVE = 0x49;
static const auto SCSIOP_PAUSE_RESUME = 0x4B;
static const auto SCSIOP_LOG_SELECT = 0x4C;
static const auto SCSIOP_LOG_SENSE = 0x4D;

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
            hr = GetLastError();
            Log::Error("Failed SetupDiEnumDeviceInterfaces [{}]", SystemError(hr));
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
            Log::Error(L"Failed SetupDiGetDeviceInterfaceDetail [{}]", SystemError(hr));
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
        Log::Error("Failed SetupDiGetDeviceInterfaceDetail [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT(&interfaceDetailData) { free(interfaceDetailData); }
    BOOST_SCOPE_EXIT_END;

    //
    // Now we have the device path. Open the device interface
    // to send Pass Through command

    Log::Debug(L"Interface: {}", interfaceDetailData->DevicePath);

    aDisk.InterfacePath = interfaceDetailData->DevicePath;

    HANDLE hDevice = CreateFile(
        interfaceDetailData->DevicePath,  // device interface name
        GENERIC_READ | GENERIC_WRITE,  // dwDesiredAccess
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // dwShareMode
        NULL,  // lpSecurityAttributes
        OPEN_EXISTING,  // dwCreationDistribution
        0,  // dwFlagsAndAttributes
        NULL  // hTemplateFile
    );
    BOOST_SCOPE_EXIT(&hDevice) { CloseHandle(hDevice); }
    BOOST_SCOPE_EXIT_END;

    //
    // We have the handle to talk to the device.
    // So we can release the interfaceDetailData buffer
    //

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CreateFile for disk interface [{}]", SystemError(hr));
        return hr;
    }

    STORAGE_PROPERTY_QUERY query;
    query.PropertyId = StorageAdapterProperty;
    query.QueryType = PropertyStandardQuery;

    DWORD returnedLength = 0L;
    CBinaryBuffer Buf;
    Buf.SetCount(512);
    if (!DeviceIoControl(
            hDevice,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(STORAGE_PROPERTY_QUERY),
            Buf.GetData(),
            (DWORD)Buf.GetCount(),
            &returnedLength,
            NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed IOCTL on disk interface [{}]", SystemError(hr));
    }
    else
    {

        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;

        CBinaryBuffer outBuf;
        outBuf.SetCount(512);

        if (!DeviceIoControl(
                hDevice,
                IOCTL_STORAGE_QUERY_PROPERTY,
                &query,
                sizeof(STORAGE_PROPERTY_QUERY),
                outBuf.GetData(),
                512,
                &returnedLength,
                NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed IOCTL_STORAGE_QUERY_PROPERTY [{}]", SystemError(hr));
            return hr;
        }
    }

    SCSI_PASS_THROUGH_WITH_BUFFERS sptwb;
    ZeroMemory(&sptwb, sizeof(SCSI_PASS_THROUGH_WITH_BUFFERS));

    sptwb.Spt.Length = sizeof(SCSI_PASS_THROUGH);
    sptwb.Spt.PathId = 0;
    sptwb.Spt.TargetId = 1;
    sptwb.Spt.Lun = 0;
    sptwb.Spt.CdbLength = CDB6GENERIC_LENGTH;
    sptwb.Spt.SenseInfoLength = 24;
    sptwb.Spt.DataIn = SCSI_IOCTL_DATA_IN;
    sptwb.Spt.DataTransferLength = 192;
    sptwb.Spt.TimeOutValue = 2;
    sptwb.Spt.DataBufferOffset = offsetof(SCSI_PASS_THROUGH_WITH_BUFFERS, DataBuf);
    sptwb.Spt.SenseInfoOffset = offsetof(SCSI_PASS_THROUGH_WITH_BUFFERS, SenseBuf);
    sptwb.Spt.Cdb[0] = SCSIOP_INQUIRY;
    sptwb.Spt.Cdb[4] = 192;

    DWORD length = offsetof(SCSI_PASS_THROUGH_WITH_BUFFERS, DataBuf) + sptwb.Spt.DataTransferLength;
    DWORD returned = 0L;
    if (!DeviceIoControl(
            hDevice, IOCTL_SCSI_PASS_THROUGH, &sptwb, sizeof(SCSI_PASS_THROUGH), &sptwb, length, &returned, FALSE))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed IOCTL_SCSI_PASS_THROUGH [{}]", SystemError(hr));
        return hr;
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
    DWORD index = 0;

    for (;;)
    {
        PhysicalDisk aDisk;

        if (FAILED(hr = GetDevice(hDevInfo, index, aDisk, guidDeviceClass)))
        {
            return hr;
        }
        if (hr == S_FALSE)
            break;  // Enumeration done!

        disks.push_back(std::move(aDisk));

        index++;
    }

    return S_OK;
}

EnumDisk::~EnumDisk() {}
