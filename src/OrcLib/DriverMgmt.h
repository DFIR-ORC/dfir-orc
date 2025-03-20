//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "Utils/Result.h"
#include "Log/Log.h"

#include "safeint.h"

#pragma managed(push, off)

namespace Orc {

enum class DriverStatus
{
    Unknown,
    Inexistent,
    Installed,
    PendingStart,
    Started,
    PendingStop,
    Stopped,
    Paused,
    PendingPause,
    PendingContinue
};

enum class DriverStartupMode
{
    Auto,
    Boot,
    Demand,
    Disabled,
    System
};

class DriverMgmt;

class DriverTermination;

class Driver : public std::enable_shared_from_this<Driver>
{
    friend class DriverMgmt;
    friend class DriverTermination;

public:
    Driver(std::shared_ptr<DriverMgmt> manager, std::wstring serviceName)
        : m_strServiceName(std::move(serviceName))
        , m_manager(std::move(manager))
    {
    }
    virtual ~Driver();

    const std::wstring& Name() const { return m_strServiceName; }

    HRESULT UnInstall();
    void EnableAutoUnInstall() { m_bAutoUninstall = true; }
    void DisableAutoUnInstall() { m_bAutoUninstall = false; }

    HRESULT Start();
    HRESULT Stop();

    void EnableAutoStop() { m_bAutoStop = true; }
    void DisableAutoStop() { m_bAutoStop = false; }

    HRESULT DisableStart();

    HRESULT OpenDevicePath(std::wstring strDevicePath, DWORD dwRequiredAccess);
    HRESULT CloseDevice();

    HRESULT DeviceIoControl(
        _In_ DWORD dwIoControlCode,
        _In_reads_bytes_opt_(nInBufferSize) LPVOID lpInBuffer,
        _In_ DWORD nInBufferSize,
        _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
        _In_ DWORD nOutBufferSize,
        _Out_opt_ LPDWORD lpBytesReturned,
        _Inout_opt_ LPOVERLAPPED lpOverlapped);

    template <typename _Tout, typename _Tin>
    Result<size_t> DeviceIoControl(DWORD dwIoControlCode, const Span<_Tin>& input, const Span<_Tout>& output);

    template <typename _Tout, typename _Tin>
    Result<Buffer<_Tout>>
    DeviceIoControl(DWORD dwIoControlCode, const Span<_Tin>& input, DWORD dwExpectedOutputElements = 1);

    template <typename _Tin>
    Result<void> DeviceIoControl(DWORD dwIoControlCode, const Span<_Tin>& input);

    template <typename _Tout>
    Result<Buffer<_Tout>> DeviceIoControl(DWORD dwIoControlCode, DWORD dwExpectedOutputElements = 1);

    Result<DriverStatus> GetStatus();

private:
    HRESULT Install(const std::wstring& strX86DriverRef, const std::wstring& strX64DriverRef);

    std::wstring m_strServiceName;
    std::wstring m_strDriverRef;
    std::wstring m_strDriverFileName;
    std::wstring m_strDevicePath;
    bool m_bDeleteDriverOnClose = false;
    bool m_bAutoStop = false;
    bool m_bAutoUninstall = false;

    HANDLE m_hDevice = INVALID_HANDLE_VALUE;

    std::shared_ptr<DriverMgmt> m_manager;
};

class DriverMgmt : public std::enable_shared_from_this<DriverMgmt>
{
    friend class Driver;
    friend class DriverTermination;

public:
    DriverMgmt(const std::wstring& strComputerName = L"")
        : m_strComputerName(strComputerName) {};

    DriverMgmt(DriverMgmt&& other) = default;

    HRESULT SetTemporaryDirectory(const std::wstring& strTempDir);

    std::shared_ptr<Driver> AddDriver(
        const std::wstring& strServiceName,
        const std::wstring& strX86DriverRef,
        const std::wstring& strX64DriverRef);

    std::shared_ptr<Driver> GetDriver(const std::wstring& strServiceName);

    ~DriverMgmt() { Disconnect(); }

private:
    static HRESULT InstallDriver(__in SC_HANDLE SchSCManager, __in LPCWSTR DriverName, __in LPCWSTR ServiceExe);
    static HRESULT RemoveDriver(SC_HANDLE SchSCManager, __in LPCWSTR DriverName);
    static HRESULT StartDriver(SC_HANDLE SchSCManager, __in LPCWSTR DriverName);
    static HRESULT StopDriver(SC_HANDLE SchSCManager, __in LPCWSTR DriverName);
    static HRESULT GetDriverStatus(SC_HANDLE SchSCManager, __in LPCWSTR DriverName, __out DriverStatus& status);
    static HRESULT ManageDriver(__in LPCWSTR DriverName, __in LPCWSTR ServiceName, __in USHORT Function);
    static HRESULT SetupDriverName(WCHAR* DriverLocation, WCHAR* szDriverFileName, ULONG BufferLength);
    static HRESULT GetDriverBinaryPathName(
        __in SC_HANDLE SchSCManager,
        const WCHAR* DriverName,
        WCHAR* szDriverFileName,
        ULONG BufferLength);
    static HRESULT SetStartupMode(SC_HANDLE SchSCManager, __in LPCWSTR DriverName, __in DriverStartupMode mode);

    SC_HANDLE m_SchSCManager = NULL;
    SC_HANDLE m_SchService = NULL;
    std::wstring m_strComputerName;
    std::wstring m_strTempDir;

    std::vector<std::shared_ptr<DriverTermination>> m_pTerminationHandlers;

    HRESULT ConnectToSCM();
    HRESULT Disconnect();
};

static const int DRIVER_FUNC_INSTALL = 0x01;
static const int DRIVER_FUNC_REMOVE = 0x02;

}  // namespace Orc

#pragma managed(pop)
