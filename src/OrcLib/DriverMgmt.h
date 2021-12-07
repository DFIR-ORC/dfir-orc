//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
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

    const std::wstring& Name() const { return m_strServiceName; }

    HRESULT UnInstall();

    HRESULT Start();
    HRESULT Stop();
    HRESULT DisableStart();

    HRESULT OpenDevicePath(std::wstring strDevicePath, DWORD dwRequiredAccess);

    HRESULT DeviceIoControl(
        _In_ DWORD dwIoControlCode,
        _In_reads_bytes_opt_(nInBufferSize) LPVOID lpInBuffer,
        _In_ DWORD nInBufferSize,
        _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
        _In_ DWORD nOutBufferSize,
        _Out_opt_ LPDWORD lpBytesReturned,
        _Inout_opt_ LPOVERLAPPED lpOverlapped);

    template <typename _Tout, size_t _ToutElements, typename _Tin, size_t _TinElements>
    HRESULT DeviceIoControl(
        DWORD dwIoControlCode,
        const Orc::Buffer<_Tin, _TinElements>& input,
        Orc::Buffer<_Tout, _ToutElements>& output)
    {
        using namespace msl::utilities;

        DWORD dwBytesReturned = 0LU;
        if (auto hr = DeviceIoControl(
                dwIoControlCode,
                static_cast<LPVOID>(input.get()),
                SafeInt<DWORD>(input.size() * sizeof(_Tin)),
                static_cast<LPVOID>(output.get()),
                SafeInt<DWORD>(output.capacity() * sizeof(_Tout)),
                &dwBytesReturned,
                NULL);
            FAILED(hr))
            return hr;

        if (dwBytesReturned % sizeof(_Tout))
        {
            Orc::Log::Error(
                L"{}::DeviceIoControl({}) returned incomplete/invalid data ({} bytes returned when exepected element "
                L"size is {}",
                m_strServiceName,
                dwIoControlCode,
                dwBytesReturned,
                sizeof(_Tout));
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        output.use(dwBytesReturned / sizeof(_Tout));
        return S_OK;
    }

    template <typename _Tout, typename _Tin, size_t _TinElements>
    Orc::Result<Orc::Buffer<_Tout>> DeviceIoControl(
        DWORD dwIoControlCode,
        const Orc::Buffer<_Tin, _TinElements>& input,
        DWORD dwExpectedOutputElements = 1)
    {
        using namespace msl::utilities;

        Orc::Buffer<_Tout> output;
        DWORD dwBytesReturned = 0LU;
        output.reserve(dwExpectedOutputElements);
        if (auto hr = DeviceIoControl(
                dwIoControlCode,
                static_cast<LPVOID>(input.get()),
                SafeInt<DWORD>(input.size() * sizeof(_Tin)),
                static_cast<LPVOID>(output.get()),
                SafeInt<DWORD>(output.capacity() * sizeof(_Tout)),
                &dwBytesReturned,
                NULL);
            FAILED(hr))
            return Orc::SystemError(hr);

        if (dwBytesReturned % sizeof(_Tout))
        {
            Orc::Log::Error(
                L"{}::DeviceIoControl({}) returned incomplete/invalid data ({} bytes returned when exepected element "
                L"size is {}",
                m_strServiceName,
                dwIoControlCode,
                dwBytesReturned,
                sizeof(_Tout));
            return Orc::SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        output.use(dwBytesReturned / sizeof(_Tout));
        return output;
    }

    template <typename _Tin, size_t _TinElements>
    HRESULT DeviceIoControl(DWORD dwIoControlCode, const Orc::Buffer<_Tin, _TinElements>& input)
    {
        using namespace msl::utilities;

        DWORD dwBytesReturned = 0LU;
        if (auto hr = DeviceIoControl(
                dwIoControlCode,
                static_cast<LPVOID>(input.get()),
                SafeInt<DWORD>(input.size() * sizeof(_Tin)),
                NULL,
                0LU,
                &dwBytesReturned,
                NULL);
            FAILED(hr))
            return hr;
        return S_OK;
    }

    template <typename _Tout>
    Orc::Result<Orc::Buffer<_Tout>> DeviceIoControl(DWORD dwIoControlCode, DWORD dwExpectedOutputElements = 1)
    {
        using namespace msl::utilities;

        Orc::Buffer<_Tout> output;
        output.reserve(dwExpectedOutputElements);

        DWORD dwBytesReturned = 0LU;
        if (auto hr = DeviceIoControl(
                dwIoControlCode,
                NULL,
                0LU,
                static_cast<LPVOID>(output.get()),
                SafeInt<DWORD>(output.capacity() * sizeof(_Tout)),
                &dwBytesReturned,
                NULL);
            FAILED(hr))
            return Orc::SystemError(hr);

        if (dwBytesReturned % sizeof(_Tout))
        {
            Log::Error(
                L"{}::DeviceIoControl({}) returned incomplete/invalid data ({} bytes returned when exepected element "
                L"size is {}",
                m_strServiceName,
                dwIoControlCode,
                dwBytesReturned,
                sizeof(_Tout));
            return Orc::SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }

        output.resize(dwBytesReturned / sizeof(_Tout), false);
        return output;
    }

    Result<DriverStatus> GetStatus();

private:
    HRESULT Install(const std::wstring& strX86DriverRef, const std::wstring& strX64DriverRef);

    std::wstring m_strServiceName;
    std::wstring m_strDriverRef;
    std::wstring m_strDriverFileName;
    std::wstring m_strDevicePath;
    bool m_bDeleteDriverOnClose = false;

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
    static HRESULT InstallDriver(SC_HANDLE SchSCManager, __in LPCTSTR DriverName, __in LPCTSTR ServiceExe);
    static HRESULT RemoveDriver(SC_HANDLE SchSCManager, __in LPCTSTR DriverName);
    static HRESULT StartDriver(SC_HANDLE SchSCManager, __in LPCTSTR DriverName);
    static HRESULT StopDriver(SC_HANDLE SchSCManager, __in LPCTSTR DriverName);
    static HRESULT GetDriverStatus(SC_HANDLE SchSCManager, __in LPCTSTR DriverName, __out DriverStatus& status);
    static HRESULT ManageDriver(LPCTSTR DriverName, __in LPCTSTR ServiceName, __in USHORT Function);
    static HRESULT SetupDriverName(WCHAR* DriverLocation, WCHAR* szDriverFileName, ULONG BufferLength);
    static HRESULT GetDriverBinaryPathName(
        __in SC_HANDLE SchSCManager,
        const WCHAR* DriverName,
        WCHAR* szDriverFileName,
        ULONG BufferLength);
    static HRESULT SetStartupMode(SC_HANDLE SchSCManager, __in LPCTSTR DriverName, __in DriverStartupMode mode);

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
