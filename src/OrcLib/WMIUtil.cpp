//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "WMIUtil.h"

#include "LogFileWriter.h"

#pragma comment(lib, "wbemuuid.lib")

using namespace Orc;

HRESULT WMIUtil::Initialize()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_pLocator.CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER)))
    {
        log::Error(_L_, hr, L"Failed to create IWbemLocator object\r\n");
        return hr;
    }

    // Connect to the local root\cimv2 namespace
    // and obtain pointer pSvc to make IWbemServices calls.
    if (FAILED(hr = m_pLocator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &m_pServices)))
    {
        log::Error(_L_, hr, L"Could not connect to WbemServices object\r\n");
        return hr;
    }

    log::Verbose(_L_, L"Connected to ROOT\\CIMV2 WMI namespace\r\n");

    if (FAILED(
            hr = CoSetProxyBlanket(
                m_pServices,  // Indicates the proxy to set
                RPC_C_AUTHN_WINNT,  // RPC_C_AUTHN_xxx
                RPC_C_AUTHZ_NONE,  // RPC_C_AUTHZ_xxx
                NULL,  // Server principal name
                RPC_C_AUTHN_LEVEL_CALL,  // RPC_C_AUTHN_LEVEL_xxx
                RPC_C_IMP_LEVEL_IMPERSONATE,  // RPC_C_IMP_LEVEL_xxx
                NULL,  // client identity
                EOAC_NONE)))
    {
        log::Error(_L_, hr, L"Could not set proxy blanket on WbemServices proxy\r\n");
        return hr;
    }
    return S_OK;
}

HRESULT WMIUtil::WMICreateProcess(
    LPCWSTR szCurrentDirectory,
    LPCWSTR szCommandLine,
    DWORD dwCreationFlags,
    DWORD dwPriority,
    DWORD& dwStatus)
{
    HRESULT hr = E_FAIL;

    // set up to call the Win32_Process::Create method
    CComBSTR ClassName(L"Win32_Process");
    CComPtr<IWbemClassObject> pProcessClass;
    if (FAILED(hr = m_pServices->GetObject(ClassName, 0, NULL, &pProcessClass, NULL)))
    {
        log::Error(_L_, hr, L"Could not GetObject of class name Win32_Process\r\n");
        return hr;
    }

    CComBSTR MethodName(L"Create");
    CComPtr<IWbemClassObject> pInParamsDefinition;
    if (FAILED(hr = pProcessClass->GetMethod(MethodName, 0, &pInParamsDefinition, NULL)))
    {
        log::Error(_L_, hr, L"Could not GetMethod Create of class Win32_Process\r\n");
        return hr;
    }

    CComPtr<IWbemClassObject> pProcessInstance;
    if (FAILED(hr = pInParamsDefinition->SpawnInstance(0, &pProcessInstance)))
    {
        log::Error(_L_, hr, L"Could not SpawnInstance of Create of class Win32_Process\r\n");
        return hr;
    }

    // Create the values for the in parameters
    CComVariant varCommand = szCommandLine;

    // Store the value for the in parameters
    if (FAILED(hr = pProcessInstance->Put(L"CommandLine", 0, &varCommand, 0)))
    {
        log::Error(_L_, hr, L"Could not put CommandLine parameter of Create of class Win32_Process\r\n");
        return hr;
    }
    log::Verbose(_L_, L"The command is: %s\r\n", varCommand.bstrVal);

    if (szCurrentDirectory != nullptr)
    {
        // Create the values for the in parameters
        CComVariant varCurrentDirectory = szCurrentDirectory;

        // Store the value for the in parameters
        if (FAILED(hr = pProcessInstance->Put(L"CurrentDirectory", 0, &varCurrentDirectory, 0)))
        {
            log::Error(_L_, hr, L"Could not put CurrentDirectory parameter of Create of class Win32_Process\r\n");
            return hr;
        }
        log::Verbose(_L_, L"The CurrentDirectory is: %s\r\n", varCurrentDirectory.bstrVal);
    }

    //************************************
    // Query the Win32_ProcessStartup Class
    CComPtr<IWbemClassObject> pProcessStartupClass;
    CComBSTR bstrProcessStartup(L"Win32_ProcessStartup");
    if (FAILED(hr = m_pServices->GetObject(bstrProcessStartup, 0, NULL, &pProcessStartupClass, NULL)))
    {
        log::Error(_L_, hr, L"Could not GetObject of class Win32_ProcessStartup\r\n");
        return hr;
    }

    // Create a Instance of Win32_ProcessStartup
    CComPtr<IWbemClassObject> pProcessStartupInstance;
    if (FAILED(hr = pProcessStartupClass->SpawnInstance(0, &pProcessStartupInstance)))
    {
        log::Error(_L_, hr, L"Could not SpawnInstance of class Win32_ProcessStartup\r\n");
        return hr;
    }

    CComVariant varCommand_ShowWindow;
    varCommand_ShowWindow = SW_HIDE;
    if (FAILED(hr = pProcessStartupInstance->Put(L"ShowWindow", 0, &varCommand_ShowWindow, 0)))
    {
        log::Error(_L_, hr, L"Could not put ShowWindow of class Win32_ProcessStartup\r\n");
        return hr;
    }
    log::Verbose(_L_, L"PriorityClass set to 0x%lx in class Win32_ProcessStartup\r\n", varCommand_ShowWindow.uintVal);

    if (dwPriority)
    {
        CComVariant varCommand_PriorityClass;
        varCommand_PriorityClass = (UINT32)dwPriority;
        if (FAILED(hr = pProcessStartupInstance->Put(L"PriorityClass", 0, &varCommand_PriorityClass, 0)))
        {
            log::Error(
                _L_, hr, L"Could not put PriorityClass of class Win32_ProcessStartup with 0x%.8lx\r\n", dwPriority);
            return hr;
        }
        log::Verbose(_L_, L"PriorityClass set to 0x%.8lx in class Win32_ProcessStartup\r\n", dwPriority);
    }

    if (dwCreationFlags)
    {
        CComVariant varCommand_CreateFlags;
        varCommand_CreateFlags = (INT32)dwCreationFlags;
        varCommand.ChangeType(VT_UI4);

        if (FAILED(hr = pProcessStartupInstance->Put(L"CreateFlags", 0, &varCommand_CreateFlags, 0L)))
        {
            log::Error(_L_, hr, L"Could not put CreateFlags of class Win32_ProcessStartup 0x%lX\r\n", dwCreationFlags);
            return hr;
        }
        log::Verbose(_L_, L"CreateFalgs set to 0x%lX in class Win32_ProcessStartup\r\n", dwCreationFlags);
    }

    // Create a command to set ProcessStartupInformation to be the instance of Win32_ProcessStartup
    CComVariant varCommand_ProcessStartup;
    varCommand_ProcessStartup = pProcessStartupInstance;
    // set the value to the instance of Win32_Process process
    if (FAILED(hr = pProcessInstance->Put(L"ProcessStartupInformation", 0, &varCommand_ProcessStartup, 0)))
    {
        log::Error(_L_, hr, L"Could not put ProcessStartupInformation of class Win32_Process\r\n");
        return hr;
    }

    // Execute Method
    CComPtr<IWbemClassObject> pOutParams;
    if (FAILED(hr = m_pServices->ExecMethod(ClassName, MethodName, 0, NULL, pProcessInstance, &pOutParams, NULL)))
    {
        log::Error(_L_, hr, L"Could not put execute command\r\n");
        return hr;
    }

    // To see what the method returned,
    // use the following code.  The return value will
    // be in &varReturnValue
    CComVariant varReturnValue;
    if (FAILED(hr = pOutParams->Get(CComBSTR(L"ReturnValue"), 0, &varReturnValue, NULL, 0)))
    {
        log::Error(_L_, hr, L"Could not retrieve value ReturnValue\r\n");
        return hr;
    }
    log::Verbose(_L_, L"Command was successfully created, ReturnValue=%d\r\n", varReturnValue.uintVal);
    dwStatus = varReturnValue.uintVal;
    return S_OK;
}

HRESULT WMIUtil::WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives)
{
    HRESULT hr = E_FAIL;

    CComPtr<IEnumWbemClassObject> pEnumerator;

    if (FAILED(
            hr = m_pServices->ExecQuery(
                bstr_t("WQL"),
                bstr_t("SELECT DeviceID FROM Win32_DiskDrive"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                NULL,
                &pEnumerator)))
    {
        log::Error(_L_, hr, L"Could not enumerate PhysicalMedia\r\n");
        return hr;
    }

    ULONG uReturn = 0;

    while (pEnumerator)
    {
        CComPtr<IWbemClassObject> pclsObj;

        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

        if (0 == uReturn)
        {
            break;
        }

        CComVariant vtProp;

        // Get the value of the Name property
        hr = pclsObj->Get(L"DeviceID", 0, &vtProp, 0, 0);

        physicaldrives.push_back(vtProp.bstrVal);
    }

    return S_OK;
}

WMIUtil::~WMIUtil() {}
