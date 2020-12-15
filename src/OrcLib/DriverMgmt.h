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

class ORCLIB_API Driver : public std::enable_shared_from_this<Driver>
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

    Result<DriverStatus> GetStatus();

private:
    HRESULT Install(const std::wstring& strX86DriverRef, const std::wstring& strX64DriverRef);

    std::wstring m_strServiceName;
    std::wstring m_strDriverRef;
    std::wstring m_strDriverFileName;
    bool m_bDeleteDriverOnClose = false;

    std::shared_ptr<DriverMgmt> m_manager;
};

class ORCLIB_API DriverMgmt : public std::enable_shared_from_this<DriverMgmt>
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
