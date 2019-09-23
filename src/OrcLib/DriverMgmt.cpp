//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "DriverMgmt.h"
#include "LogFileWriter.h"
#include "EmbeddedResource.h"
#include "ParameterCheck.h"
#include "Temporary.h"
#include "SystemDetails.h"
#include "Robustness.h"

#include "Buffer.h"

#include "boost\scope_exit.hpp"

using namespace Orc;

class Orc::DriverTermination : public TerminationHandler
{

    friend class DriverMgmt;
    friend class Driver;

public:
    DriverTermination(logger pLog, const std::shared_ptr<Driver>& driver)
        : TerminationHandler(driver->Name(), ROBUSTNESS_TEMPFILE)
        , m_driver(driver)
        , _L_(std::move(pLog)) {};

    HRESULT operator()();

private:
    logger _L_;
    std::weak_ptr<Driver> m_driver;
};

HRESULT DriverTermination::operator()()
{

    HRESULT hr = E_FAIL;

    auto driver = m_driver.lock();

    if (driver == nullptr)
        return S_OK;  // Driver is already deleted

    if (!driver->Name().empty())
    {

        SC_HANDLE SchSCManager =
            OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT | SC_MANAGER_ALL_ACCESS);

        if (SchSCManager == NULL)
        {
            log::Error(
                driver->_L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"OpenSCManager failed in termination handler\r\n");
        }
        else
        {
            BOOST_SCOPE_EXIT(&SchSCManager)
            {
                if (SchSCManager != NULL)
                    CloseServiceHandle(SchSCManager);
            }
            BOOST_SCOPE_EXIT_END;

            if (FAILED(hr = Orc::DriverMgmt::StopDriver(_L_, SchSCManager, driver->Name().c_str())))
            {
                log::Error(_L_, hr = hr, L"StopDriver failed in termination handler\r\n");
            }
            else
            {
                log::Verbose(_L_, L"Successfully stopped driver %s\r\n", driver->Name().c_str());
                if (FAILED(hr = Orc::DriverMgmt::RemoveDriver(_L_, SchSCManager, driver->Name().c_str())))
                {
                    log::Error(_L_, hr = hr, L"RemoveDriver failed in termination handler\r\n");
                }
                else
                {
                    log::Verbose(_L_, L"Successfully removed driver %s\r\n", driver->Name().c_str());
                }
            }
        }
    }
    if (!driver->m_strDriverFileName.empty())
    {
        if (FAILED(hr = UtilDeleteTemporaryFile(_L_, driver->m_strDriverFileName.c_str())))
        {
            log::Error(
                _L_,
                hr = hr,
                L"Attempt to remove driver file %s in terminationhandler failed\r\n",
                driver->m_strDriverFileName.c_str());
            return hr;
        }
    }
    return S_OK;
}

HRESULT DriverMgmt::ConnectToSCM()
{
    HRESULT hr = E_FAIL;

    if (m_SchSCManager != NULL)
        return S_OK;

    m_SchSCManager = OpenSCManager(
        m_strComputerName.empty() ? NULL : m_strComputerName.c_str(),
        SERVICES_ACTIVE_DATABASE,
        SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);

    if (m_SchSCManager == NULL)
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to connect to Service Control Manager\r\n");
        return hr;
    }
    return S_OK;
}

HRESULT DriverMgmt::Disconnect()
{
    if (m_SchService != NULL)
    {
        auto temp = m_SchService;
        m_SchService = NULL;
        CloseServiceHandle(temp);
    }
    if (m_SchSCManager != NULL)
    {
        auto temp = m_SchSCManager;
        m_SchSCManager = NULL;
        CloseServiceHandle(temp);
    }
    return S_OK;
}

HRESULT DriverMgmt::SetTemporaryDirectory(const std::wstring& strTempDir)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = GetOutputDir(strTempDir.c_str(), m_strTempDir, true)))
    {
        log::Error(_L_, hr, L"Failed to create temporary directory %s\r\n", strTempDir.c_str());
        return hr;
    }
    return S_OK;
}

HRESULT Orc::Driver::Install(const std::wstring& strX86DriverRef, const std::wstring& strX64DriverRef)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_manager->ConnectToSCM()))
        return hr;

    WORD wArch = 0;
    if (FAILED(hr = SystemDetails::GetArchitecture(wArch)))
        return hr;

    switch (wArch)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            m_strDriverRef = strX86DriverRef;
            break;
        case PROCESSOR_ARCHITECTURE_AMD64:
            m_strDriverRef = strX64DriverRef;
            break;
        default:
            return E_FAIL;
    }

    auto pTerminationHandler = std::make_shared<DriverTermination>(m_manager->_L_, shared_from_this());

    std::wstring strDriverFileName;

    log::Verbose(m_manager->_L_, L"ExtensionLibrary: Loading value %s\r\n", m_strDriverRef.c_str());
    std::wstring strNewLibRef;
    if (SUCCEEDED(EmbeddedResource::ExtractValue(m_manager->_L_, L"", m_strDriverRef, strNewLibRef)))
    {
        log::Verbose(
            m_manager->_L_,
            L"ExtensionLibrary: Loaded value %s=%s successfully\r\n",
            m_strDriverRef.c_str(),
            strNewLibRef.c_str());
        if (EmbeddedResource::IsResourceBased(strNewLibRef))
        {
            if (m_manager->m_strTempDir.empty())
            {
                if (FAILED(hr = m_manager->SetTemporaryDirectory(L"%TEMP%")))
                    return hr;
            }

            if (FAILED(
                    hr = EmbeddedResource::ExtractToFile(
                        m_manager->_L_,
                        strNewLibRef,
                        m_strServiceName,
                        RESSOURCE_READ_EXECUTE_BA,
                        m_manager->m_strTempDir,
                        m_strDriverFileName)))
            {
                log::Error(
                    m_manager->_L_,
                    hr,
                    L"Failed to extract driver resource %s into %s\r\n",
                    strNewLibRef.c_str(),
                    m_manager->m_strTempDir.c_str());
                return hr;
            }
        }
    }
    else
    {
        if (EmbeddedResource::IsResourceBased(m_strDriverRef))
        {
            if (m_manager->m_strTempDir.empty())
            {
                if (FAILED(hr = m_manager->SetTemporaryDirectory(L"%TEMP%")))
                    return hr;
            }

            if (FAILED(
                    hr = EmbeddedResource::ExtractToFile(
                        m_manager->_L_,
                        m_strDriverRef,
                        m_strServiceName,
                        RESSOURCE_READ_EXECUTE_BA,
                        m_manager->m_strTempDir,
                        strDriverFileName)))
            {
                log::Error(
                    m_manager->_L_,
                    hr,
                    L"Failed to extract driver resource %s into %s\r\n",
                    m_strDriverRef.c_str(),
                    m_manager->m_strTempDir.c_str());
                return hr;
            }
        }
        else
        {
            m_strDriverFileName = m_strDriverRef;
        }
    }

    if (FAILED(
            hr = DriverMgmt::InstallDriver(
                m_manager->_L_, m_manager->m_SchSCManager, m_strServiceName.c_str(), m_strDriverFileName.c_str())))
    {
        log::Error(
            m_manager->_L_,
            hr,
            L"Failed to install driver %s with service name %s\r\n",
            m_strDriverRef.c_str(),
            m_strServiceName.c_str());

        if (m_bDeleteDriverOnClose)
        {
            if (FAILED(hr = UtilDeleteTemporaryFile(m_manager->_L_, m_strDriverFileName.c_str())))
            {
                log::Error(
                    m_manager->_L_,
                    hr,
                    L"Failed to delete temporary driver file name %s\r\n",
                    m_strDriverFileName.c_str());
                return hr;
            }
        }
        return hr;
    }

    pTerminationHandler->m_driver = shared_from_this();

    m_manager->m_pTerminationHandlers.push_back(pTerminationHandler);
    Robustness::AddTerminationHandler(pTerminationHandler);

    return S_OK;
}

std::shared_ptr<Orc::Driver> DriverMgmt::GetDriver(
    const std::wstring& strServiceName,
    const std::wstring& strX86DriverRef,
    const std::wstring& strX64DriverRef)
{
    auto retval = std::make_shared<Driver>(_L_, shared_from_this(), strServiceName);

    if (auto hr = retval->Install(strX86DriverRef, strX64DriverRef); FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to install driver %s\r\n", strServiceName.c_str());
        return nullptr;
    }
    return retval;
}

HRESULT Driver::UnInstall()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_manager->ConnectToSCM()))
        return hr;

    if (FAILED(hr = DriverMgmt::RemoveDriver(m_manager->_L_, m_manager->m_SchSCManager, m_strServiceName.c_str())))
    {
        log::Error(m_manager->_L_, hr, L"RemoveDriver(%s) failed\r\n", m_strServiceName.c_str());
        return hr;
    }

    for (auto phandler : m_manager->m_pTerminationHandlers)
    {
        auto driver = phandler->m_driver.lock();
        if (driver)
        {
            if (driver->m_strServiceName == m_strServiceName)
            {
                Robustness::RemoveTerminationHandler(phandler);

                if (!driver->m_strDriverFileName.empty() && driver->m_bDeleteDriverOnClose)
                {
                    if (FAILED(hr = UtilDeleteTemporaryFile(m_manager->_L_, driver->m_strDriverFileName.c_str())))
                    {
                        log::Error(
                            m_manager->_L_,
                            hr,
                            L"Failed to delete temporary driver file %s\r\n",
                            driver->m_strDriverFileName.c_str());
                    }
                }
            }
        }
    }

    return S_OK;
}

HRESULT Driver::Start()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_manager->ConnectToSCM()))
        return hr;

    if (FAILED(hr = DriverMgmt::StartDriver(m_manager->_L_, m_manager->m_SchSCManager, m_strServiceName.c_str())))
    {
        log::Error(m_manager->_L_, hr, L"Failed to start driver %s\r\n", m_strServiceName.c_str());
        return hr;
    }
    return S_OK;
}
HRESULT Driver::Stop()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_manager->ConnectToSCM()))
        return hr;

    if (FAILED(hr = DriverMgmt::StopDriver(m_manager->_L_, m_manager->m_SchSCManager, m_strServiceName.c_str())))
    {
        if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE)  // We ignore error if driver was already stopped...
        {
            log::Error(m_manager->_L_, hr, L"Failed to stop driver %s\r\n", m_strServiceName.c_str());
            return hr;
        }
    }
    return S_OK;
}

ServiceStatus Orc::Driver::GetStatus()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_manager->ConnectToSCM()))
        return ServiceStatus::Inexistent;

    return ServiceStatus();
}

HRESULT Orc::DriverMgmt::InstallDriver(
    const logger& pLog,
    __in SC_HANDLE SchSCManager,
    __in LPCTSTR DriverName,
    __in LPCTSTR ServiceExe)

{
    SC_HANDLE schService = NULL;
    HRESULT hr = E_FAIL;

    //
    // NOTE: This creates an entry for a standalone driver. If this
    //       is modified for use with a driver that requires a Tag,
    //       Group, and/or Dependencies, it may be necessary to
    //       query the registry for existing driver information
    //       (in order to determine a unique Tag, etc.).
    //

    //
    // Create a new a service object.
    //
    schService = CreateServiceW(
        SchSCManager,  // handle of service control manager database
        DriverName,  // address of name of service to start
        DriverName,  // address of display name
        SERVICE_ALL_ACCESS,  // type of access to service
        SERVICE_KERNEL_DRIVER,  // type of service
        SERVICE_SYSTEM_START,  // when to start service
        SERVICE_ERROR_NORMAL,  // severity if service fails to start
        ServiceExe,  // address of name of binary file
        L"Base",  // service does not belong to a group
        NULL,  // no tag requested
        NULL,  // no dependency names
        NULL,  // use LocalSystem account
        NULL  // no password for service account
    );

    BOOST_SCOPE_EXIT(&schService)
    {
        if (schService != NULL)
        {
            CloseServiceHandle(schService);
        }
    }
    BOOST_SCOPE_EXIT_END;

    if (schService == NULL)
    {

        if (GetLastError() == ERROR_SERVICE_EXISTS)
        {
            //
            // Ignore this error, make sure the parameters are rigth
            //
            schService = OpenServiceW(SchSCManager, DriverName, SERVICE_CHANGE_CONFIG);

            if (schService == NULL)
            {
                log::Error(pLog, HRESULT_FROM_WIN32(GetLastError()), L"OpenService failed\r\n");
                return hr;
            }
            else
            {
                if (!ChangeServiceConfigW(
                        schService,
                        SERVICE_NO_CHANGE,
                        SERVICE_SYSTEM_START,
                        SERVICE_NO_CHANGE,
                        ServiceExe,
                        NULL,
                        NULL,
                        NULL,
                        NULL,
                        NULL,
                        NULL))
                {
                    log::Error(pLog, HRESULT_FROM_WIN32(GetLastError()), L"ChangeServiceConfig failed\r\n");
                    return hr;
                }
            }
            return S_OK;
        }
        else
        {

            log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"CreateService failed\r\n");
            return hr;
        }
    }

    return S_OK;
}

HRESULT Orc::DriverMgmt::ManageDriver(
    const logger& pLog,
    __in LPCTSTR DriverName,
    __in LPCTSTR ServiceName,
    __in USHORT Function)
{
    HRESULT hr = E_FAIL;
    SC_HANDLE schSCManager;

    //
    // Insure (somewhat) that the driver and service names are valid.
    if (!DriverName || !ServiceName)
    {
        log::Info(pLog, L"Invalid Driver or Service provided to ManageDriver()\r\n");
        return E_INVALIDARG;
    }

    //
    // Connect to the Service Control Manager and open the Services database.
    schSCManager = OpenSCManager(
        NULL,  // local machine
        NULL,  // local database
        SC_MANAGER_ALL_ACCESS  // access required
    );

    if (!schSCManager)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log::Info(pLog, L"Open SC Manager failed! Error = 0x%lx\r\n", hr);
        return hr;
    }

    //
    // Do the requested function.
    switch (Function)
    {
        case DRIVER_FUNC_INSTALL:
            //
            // Install the driver service.
            if (FAILED(hr = InstallDriver(pLog, schSCManager, DriverName, ServiceName)))
            {
                //
                // Indicate an error.
                hr = E_FAIL;
            }
            break;

        case DRIVER_FUNC_REMOVE:
            //
            // Stop the driver.
            static_cast<void>(StopDriver(pLog, schSCManager, DriverName));
            //
            // Remove the driver service.
            static_cast<void>(RemoveDriver(pLog, schSCManager, DriverName));
            //
            // Ignore all errors.
            hr = S_OK;
            break;

        default:
            log::Info(pLog, L"Unknown ManageDriver() function.\r\n");
            hr = E_INVALIDARG;
            break;
    }

    //
    // Close handle to service control manager.
    if (schSCManager)
        CloseServiceHandle(schSCManager);

    return hr;
}  // ManageDriver

HRESULT Orc::DriverMgmt::RemoveDriver(const logger& pLog, __in SC_HANDLE SchSCManager, __in LPCTSTR DriverName)
{
    HRESULT hr = E_FAIL;
    SC_HANDLE schService;

    //
    // Open the handle to the existing service.
    schService = OpenService(SchSCManager, DriverName, DELETE);

    BOOST_SCOPE_EXIT(&schService)
    {
        // Close the service object.
        if (schService)
        {
            SC_HANDLE temp = schService;
            schService = NULL;
            CloseServiceHandle(temp);
        }
    }
    BOOST_SCOPE_EXIT_END;

    if (schService == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"OpenService failed!\r\n");
        return hr;
    }

    //
    // Mark the service for deletion from the service control manager database.
    if (!DeleteService(schService))
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"DeleteService failed!\r\n");
        return hr;
    }

    return S_OK;
}  // RemoveDriver

HRESULT Orc::DriverMgmt::StartDriver(const logger& pLog, __in SC_HANDLE SchSCManager, __in LPCTSTR DriverName)
{
    HRESULT hr = E_FAIL;

    SC_HANDLE schService;

    //
    // Open the handle to the existing service.
    //

    schService = OpenService(SchSCManager, DriverName, SERVICE_START);
    BOOST_SCOPE_EXIT(&schService)
    {
        // Close the service object.
        if (schService)
        {
            SC_HANDLE temp = schService;
            schService = NULL;
            CloseServiceHandle(temp);
        }
    }
    BOOST_SCOPE_EXIT_END;

    if (schService == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"OpenService failed!\r\n");
        return hr;
    }

    //
    // Start the execution of the service (i.e. start the driver).
    if (!StartService(schService, 0, NULL))
    {
        DWORD err = GetLastError();

        if (err == ERROR_SERVICE_ALREADY_RUNNING)
        {
            // Ignore this error.
            return S_OK;
        }
        else
        {
            log::Error(pLog, hr = HRESULT_FROM_WIN32(err), L"StartService failure!\r\n");
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::DriverMgmt::StopDriver(const logger& pLog, __in SC_HANDLE SchSCManager, __in LPCTSTR DriverName)
{
    HRESULT hr = E_FAIL;
    SC_HANDLE schService;
    SERVICE_STATUS serviceStatus;

    //
    // Open the handle to the existing service.
    schService = OpenService(SchSCManager, DriverName, SERVICE_STOP);
    BOOST_SCOPE_EXIT(&schService)
    {
        // Close the service object.
        if (schService)
        {
            SC_HANDLE temp = schService;
            schService = NULL;
            CloseServiceHandle(temp);
        }
    }
    BOOST_SCOPE_EXIT_END;

    if (schService == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"OpenService failed!\r\n");
        return hr;
    }

    //
    // Request that the service stop.
    if (!ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus))
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"ControlService failed!\r\n");
        return hr;
    }

    return S_OK;
}  //  StopDriver

HRESULT Orc::DriverMgmt::GetDriverStatus(const logger& pLog, SC_HANDLE SchSCManager, LPCTSTR DriverName)
{
    HRESULT hr = E_FAIL;
    SC_HANDLE schService;

    //
    // Open the handle to the existing service.
    schService = OpenService(SchSCManager, DriverName, SERVICE_QUERY_STATUS);
    BOOST_SCOPE_EXIT(&schService)
    {
        // Close the service object.
        if (schService)
        {
            SC_HANDLE temp = schService;
            schService = NULL;
            CloseServiceHandle(temp);
        }
    }
    BOOST_SCOPE_EXIT_END;

    if (schService == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"OpenService failed!\r\n");
        return hr;
    }

    //
    // Request that the service stop.
    Buffer<BYTE, sizeof(SERVICE_STATUS_PROCESS)> serviceStatus;
    DWORD cbNeeded = 0L;
    if (!QueryServiceStatusEx(
            schService, SC_STATUS_PROCESS_INFO, serviceStatus.get(), serviceStatus.capacity(), &cbNeeded))
    {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            serviceStatus.reserve(cbNeeded);
            if (!QueryServiceStatusEx(
                    schService, SC_STATUS_PROCESS_INFO, serviceStatus.get(), serviceStatus.capacity(), &cbNeeded))
            {
                log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatusEx failed!\r\n");
                return hr;
            }
        }
        else
        {
            log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"QueryServiceStatusEx failed!\r\n");
            return hr;
        }
    }
    serviceStatus.use(serviceStatus.capacity());

    auto pStatus = serviceStatus.get_as<SERVICE_STATUS_PROCESS>();

    switch (pStatus->dwCurrentState)
    {
        case SERVICE_CONTINUE_PENDING:
            return ServiceStatus::PendingContinue;
        case SERVICE_PAUSE_PENDING:
            return ServiceStatus::PendingPause;
        case SERVICE_PAUSED:
            return ServiceStatus::Paused;
        case SERVICE_RUNNING:
            return ServiceStatus::Started;
        case SERVICE_START_PENDING:
            return ServiceStatus::PendingStart;
        case SERVICE_STOP_PENDING:
            return ServiceStatus::PendingStop;
        case SERVICE_STOPPED:
            return ServiceStatus::Stopped;
        default:
            return ServiceStatus::Inexistent;
    }
}

HRESULT
Orc::DriverMgmt::SetupDriverName(const logger& pLog, WCHAR* DriverLocation, WCHAR* szDriverFileName, ULONG BufferLength)
{
    HRESULT hr = E_FAIL;
    //
    // Get the current directory.
    //
    if (auto driverLocLen = GetCurrentDirectory(BufferLength, DriverLocation); driverLocLen == 0)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"GetCurrentDirectory failed!\r\n");
        return hr;
    }

    //
    // Setup path name to driver file.
    //
    if (FAILED(hr = StringCbCat(DriverLocation, BufferLength, L"\\")))
        return hr;

    if (FAILED(hr = StringCbCat(DriverLocation, BufferLength, szDriverFileName)))
        return hr;

    //
    // Insure driver file is in the specified directory.
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    if ((fileHandle = CreateFile(DriverLocation, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL))
        == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(pLog, hr, L"%s is not present in %s.\r\n", szDriverFileName, DriverLocation);
        //
        // Indicate failure.
        return hr;
    }

    //
    // Close open file handle.
    if (fileHandle != INVALID_HANDLE_VALUE)
        CloseHandle(fileHandle);

    //
    // Indicate success.
    return S_OK;
}  // SetupDriverName

HRESULT Orc::DriverMgmt::GetDriverBinaryPathName(
    const logger& pLog,
    __in SC_HANDLE SchSCManager,
    const WCHAR* DriverName,
    WCHAR* szDriverFileName,
    ULONG BufferLength)
{
    HRESULT hr = E_FAIL;
    SC_HANDLE schService;

    //
    // Open the handle to the existing service.
    schService = OpenService(SchSCManager, DriverName, SERVICE_ALL_ACCESS);
    if (schService == NULL)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"OpenService %s failed!\r\n", DriverName);
        return hr;
    }

    BOOST_SCOPE_EXIT(&schService)
    {
        if (schService)
        {
            CloseServiceHandle(schService);
            schService = NULL;
        }
    }
    BOOST_SCOPE_EXIT_END;

    //
    // Request the service configuration.
    std::vector<uint8_t> serviceConfig;
    LPQUERY_SERVICE_CONFIG pServiceConfig = NULL;
    DWORD cbBytesNeeded = 0;

    DWORD lastError = ERROR_INSUFFICIENT_BUFFER;
    while (lastError == ERROR_INSUFFICIENT_BUFFER)
    {
        if (cbBytesNeeded)
        {
            serviceConfig.resize(cbBytesNeeded);
            pServiceConfig = reinterpret_cast<LPQUERY_SERVICE_CONFIG>(serviceConfig.data());
        }

        QueryServiceConfig(schService, pServiceConfig, cbBytesNeeded, &cbBytesNeeded);
        lastError = GetLastError();
    }

    if (lastError != ERROR_SUCCESS)
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(lastError), L"QueryServiceConfig %s failed!\r\n", DriverName);
        return hr;
    }

    wcscpy_s(szDriverFileName, BufferLength, pServiceConfig->lpBinaryPathName);
    return S_OK;
}
