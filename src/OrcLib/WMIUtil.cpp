//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "WMIUtil.h"

#pragma comment(lib, "wbemuuid.lib")

using namespace Orc;

HRESULT WMI::Initialize()
{
    HRESULT hr = E_FAIL;

    if (!m_pLocator)
    {
        if (FAILED(hr = m_pLocator.CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER)))
        {
            Log::Error(L"Failed to create IWbemLocator object [{}]", SystemError(hr));
            return hr;
        }
    }

    if (!m_pServices)
    {
        // Connect to the local root\cimv2 namespace
        // and obtain pointer pSvc to make IWbemServices calls.
        const Guard::ComBStr nameSpace(L"ROOT\\CIMV2");
        if (FAILED(hr = m_pLocator->ConnectServer(nameSpace, NULL, NULL, 0, NULL, 0, 0, &m_pServices)))
        {
            Log::Error(L"Could not connect to WbemServices object [{}]", SystemError(hr));
            return hr;
        }
    }

    Log::Debug("Connected to ROOT\\CIMV2 WMI namespace");

    hr = CoSetProxyBlanket(
        m_pServices,  // Indicates the proxy to set
        RPC_C_AUTHN_WINNT,  // RPC_C_AUTHN_xxx
        RPC_C_AUTHZ_NONE,  // RPC_C_AUTHZ_xxx
        NULL,  // Server principal name
        RPC_C_AUTHN_LEVEL_CALL,  // RPC_C_AUTHN_LEVEL_xxx
        RPC_C_IMP_LEVEL_IMPERSONATE,  // RPC_C_IMP_LEVEL_xxx
        NULL,  // client identity
        EOAC_NONE);

    if (FAILED(hr))
    {
        Log::Error(L"Could not set proxy blanket on WbemServices proxy [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT WMI::WMICreateProcess(
    LPCWSTR szCurrentDirectory,
    LPCWSTR szCommandLine,
    DWORD dwCreationFlags,
    DWORD dwPriority,
    DWORD& dwStatus)
{
    HRESULT hr = E_FAIL;

    // set up to call the Win32_Process::Create method
    Guard::ComBStr ClassName(L"Win32_Process");
    Guard::ComPtr<IWbemClassObject> pProcessClass;
    if (FAILED(hr = m_pServices->GetObject(ClassName, 0, NULL, &pProcessClass, NULL)))
    {
        Log::Error(L"Could not GetObject of class name Win32_Process [{}]", SystemError(hr));
        return hr;
    }

    Guard::ComBStr MethodName(L"Create");
    Guard::ComPtr<IWbemClassObject> pInParamsDefinition;
    if (FAILED(hr = pProcessClass->GetMethod(MethodName, 0, &pInParamsDefinition, NULL)))
    {
        Log::Error(L"Could not GetMethod Create of class Win32_Process [{}]", SystemError(hr));
        return hr;
    }

    Guard::ComPtr<IWbemClassObject> pProcessInstance;
    if (FAILED(hr = pInParamsDefinition->SpawnInstance(0, &pProcessInstance)))
    {
        Log::Error("Could not SpawnInstance of Create of class Win32_Process [{}]", SystemError(hr));
        return hr;
    }

    // Create the values for the in parameters
    Guard::ComVariant varCommand(szCommandLine);

    // Store the value for the in parameters
    if (FAILED(hr = pProcessInstance->Put(L"CommandLine", 0, &varCommand, 0)))
    {
        Log::Error("Could not put CommandLine parameter of Create of class Win32_Process [{}]", SystemError(hr));
        return hr;
    }
    Log::Debug(L"The command is: {}", varCommand.bstrVal);

    if (szCurrentDirectory != nullptr)
    {
        // Create the values for the in parameters
        Guard::ComVariant varCurrentDirectory(szCurrentDirectory);

        // Store the value for the in parameters
        if (FAILED(hr = pProcessInstance->Put(L"CurrentDirectory", 0, &varCurrentDirectory, 0)))
        {
            Log::Error(
                "Could not put CurrentDirectory parameter of Create of class Win32_Process [{}]", SystemError(hr));
            return hr;
        }

        Log::Debug(L"The CurrentDirectory is: {}", varCurrentDirectory.bstrVal);
    }

    //************************************
    // Query the Win32_ProcessStartup Class
    Guard::ComPtr<IWbemClassObject> pProcessStartupClass;
    Guard::ComBStr bstrProcessStartup(L"Win32_ProcessStartup");
    if (FAILED(hr = m_pServices->GetObject(bstrProcessStartup, 0, NULL, &pProcessStartupClass, NULL)))
    {
        Log::Error(L"Could not GetObject of class Win32_ProcessStartup");
        return hr;
    }

    // Create a Instance of Win32_ProcessStartup
    Guard::ComPtr<IWbemClassObject> pProcessStartupInstance;
    if (FAILED(hr = pProcessStartupClass->SpawnInstance(0, &pProcessStartupInstance)))
    {
        Log::Error(L"Could not SpawnInstance of class Win32_ProcessStartup");
        return hr;
    }

    Guard::ComVariant varCommand_ShowWindow(static_cast<INT32>(SW_HIDE));
    if (FAILED(hr = pProcessStartupInstance->Put(L"ShowWindow", 0, &varCommand_ShowWindow, 0)))
    {
        Log::Error("Could not put ShowWindow of class Win32_ProcessStartup");
        return hr;
    }
    Log::Debug("PriorityClass set to {:#x} in class Win32_ProcessStartup", varCommand_ShowWindow.uintVal);

    if (dwPriority)
    {
        Guard::ComVariant varCommand_PriorityClass(static_cast<UINT32>(dwPriority));
        if (FAILED(hr = pProcessStartupInstance->Put(L"PriorityClass", 0, &varCommand_PriorityClass, 0)))
        {
            Log::Error(
                "Could not put PriorityClass of class Win32_ProcessStartup with {:#x} [{}]",
                dwPriority,
                SystemError(hr));
            return hr;
        }
        Log::Debug("PriorityClass set to {:#x} in class Win32_ProcessStartup", dwPriority);
    }

    if (dwCreationFlags)
    {
        Guard::ComVariant varCommand_CreateFlags(static_cast<INT32>(dwCreationFlags));
        varCommand.ChangeType(VT_UI4);

        if (FAILED(hr = pProcessStartupInstance->Put(L"CreateFlags", 0, &varCommand_CreateFlags, 0L)))
        {
            Log::Error("Could not put CreateFlags of class Win32_ProcessStartup {:#x}", dwCreationFlags);
            return hr;
        }
        Log::Debug("CreateFlags set to {:#x} in class Win32_ProcessStartup", dwCreationFlags);
    }

    // Create a command to set ProcessStartupInformation to be the instance of Win32_ProcessStartup
    Guard::ComVariant varCommand_ProcessStartup(pProcessStartupInstance);
    // set the value to the instance of Win32_Process process
    if (FAILED(hr = pProcessInstance->Put(L"ProcessStartupInformation", 0, &varCommand_ProcessStartup, 0)))
    {
        Log::Error("Could not put ProcessStartupInformation of class Win32_Process [{}]", SystemError(hr));
        return hr;
    }

    // Execute Method
    Guard::ComPtr<IWbemClassObject> pOutParams;
    if (FAILED(hr = m_pServices->ExecMethod(ClassName, MethodName, 0, NULL, pProcessInstance, &pOutParams, NULL)))
    {
        Log::Error("Could not put execute command [{}]", SystemError(hr));
        return hr;
    }

    // To see what the method returned,
    // use the following code.  The return value will
    // be in &varReturnValue
    Guard::ComVariant varReturnValue;
    if (FAILED(hr = pOutParams->Get(L"ReturnValue", 0, &varReturnValue, NULL, 0)))
    {
        Log::Error("Could not retrieve value ReturnValue [{}]", SystemError(hr));
        return hr;
    }
    Log::Debug("Command was successfully created, ReturnValue={}", varReturnValue.uintVal);
    dwStatus = varReturnValue.uintVal;
    return S_OK;
}

Result<Guard::ComPtr<IEnumWbemClassObject>> Orc::WMI::Query(LPCWSTR szRequest) const
{
    Guard::ComPtr<IEnumWbemClassObject> pEnumerator;
    if (auto hr = m_pServices->ExecQuery(
            Guard::ComBStr(L"WQL"),
            Guard::ComBStr(szRequest),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL,
            &pEnumerator);
        FAILED(hr))
    {
        Log::Error(L"Query '{}' failed [{}]", szRequest, SystemError(hr));
        return SystemError(hr);
    }

    return pEnumerator;
}

template <>
Result<bool> Orc::WMI::GetProperty<bool>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_BOOLEAN)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_BOOL) && FAILED(vtProp.ChangeType(VT_BOOL)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.boolVal;
}

template <>
Result<SHORT> Orc::WMI::GetProperty<SHORT>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_SINT16)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_I2) && FAILED(vtProp.ChangeType(VT_I2)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.iVal;
}

template <>
Result<USHORT> Orc::WMI::GetProperty<USHORT>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_UINT16)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_UI2) && FAILED(vtProp.ChangeType(VT_UI2)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.uiVal;
}

template <>
Result<ULONG32> Orc::WMI::GetProperty<ULONG32>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_UINT32)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_UI4) && FAILED(vtProp.ChangeType(VT_UI4)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.ulVal;
}

template <>
Result<LONG32> Orc::WMI::GetProperty<LONG32>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_SINT32 || !(vtProp.vt & VT_I4))
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_I4) && FAILED(vtProp.ChangeType(VT_I4)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.lVal;
}

template <>
Result<ULONG64> Orc::WMI::GetProperty<ULONG64>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);
    if (propType != CIM_UINT64)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_UI8) && FAILED(vtProp.ChangeType(VT_UI8)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.ullVal;
}

template <>
Result<LONG64> Orc::WMI::GetProperty<LONG64>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_SINT64)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_I8) && FAILED(vtProp.ChangeType(VT_I8)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return vtProp.llVal;
}

template <>
Result<ByteBuffer> Orc::WMI::GetProperty<ByteBuffer>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    return SystemError(E_NOTIMPL);
}

template <>
Result<std::wstring> Orc::WMI::GetProperty<std::wstring>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(std::move(hr));

    if (propType != CIM_STRING)
        return SystemError(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_BSTR) && FAILED(vtProp.ChangeType(VT_BSTR)))
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return std::wstring(vtProp.bstrVal, SysStringLen(vtProp.bstrVal));
}

template <>
Result<std::vector<std::wstring>>
Orc::WMI::GetProperty<std::vector<std::wstring>>(const Guard::ComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    Guard::ComVariant vtProp;
    CIMTYPE propType;

    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return SystemError(hr);

    if (propType != CIM_FLAG_ARRAY || !(vtProp.vt & (VT_ARRAY | VT_BSTR)))
        return SystemError(E_NOT_VALID_STATE);

    return SystemError(E_NOTIMPL);
}

HRESULT WMI::WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives) const
{
    auto result = Query(L"SELECT DeviceID FROM Win32_DiskDrive");
    if (result.has_error())
    {
        assert(result.error().category() == std::system_category());
        return result.error().value();
    }

    auto pEnumerator = *result;

    ULONG uReturn = 0;

    while (pEnumerator)
    {
        Guard::ComPtr<IWbemClassObject> pclsObj;

        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

        if (0 == uReturn)
            break;

        Guard::ComVariant vtProp;

        // Get the value of the Name property
        hr = pclsObj->Get(L"DeviceID", 0, &vtProp, 0, 0);

        physicaldrives.push_back(vtProp.bstrVal);
    }

    return S_OK;
}

WMI::~WMI() {}
