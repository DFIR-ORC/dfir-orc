//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "WMIUtil.h"

#pragma comment(lib, "wbemuuid.lib")

using namespace stx;
using namespace Orc;

HRESULT WMI::Initialize()
{
    HRESULT hr = E_FAIL;

    if (!m_pLocator)
    {
        if (FAILED(hr = m_pLocator.CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER)))
        {
            spdlog::error(L"Failed to create IWbemLocator object (code: {:#x})", hr);
            return hr;
        }
    }

    if (!m_pServices)
    {
        // Connect to the local root\cimv2 namespace
        // and obtain pointer pSvc to make IWbemServices calls.
        if (FAILED(hr = m_pLocator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &m_pServices)))
        {
            spdlog::error(L"Could not connect to WbemServices object (code: {:#x})", hr);
            return hr;
        }
    }

    spdlog::debug(L"Connected to ROOT\\CIMV2 WMI namespace");

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
        spdlog::error(L"Could not set proxy blanket on WbemServices proxy (code: {:#x})", hr);
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
    CComBSTR ClassName(L"Win32_Process");
    CComPtr<IWbemClassObject> pProcessClass;
    if (FAILED(hr = m_pServices->GetObject(ClassName, 0, NULL, &pProcessClass, NULL)))
    {
        spdlog::error(L"Could not GetObject of class name Win32_Process (code: {:#x})", hr);
        return hr;
    }

    CComBSTR MethodName(L"Create");
    CComPtr<IWbemClassObject> pInParamsDefinition;
    if (FAILED(hr = pProcessClass->GetMethod(MethodName, 0, &pInParamsDefinition, NULL)))
    {
        spdlog::error(L"Could not GetMethod Create of class Win32_Process (code: {:#x})", hr);
        return hr;
    }

    CComPtr<IWbemClassObject> pProcessInstance;
    if (FAILED(hr = pInParamsDefinition->SpawnInstance(0, &pProcessInstance)))
    {
        spdlog::error("Could not SpawnInstance of Create of class Win32_Process (code: {:#x})", hr);
        return hr;
    }

    // Create the values for the in parameters
    CComVariant varCommand = szCommandLine;

    // Store the value for the in parameters
    if (FAILED(hr = pProcessInstance->Put(L"CommandLine", 0, &varCommand, 0)))
    {
        spdlog::error("Could not put CommandLine parameter of Create of class Win32_Process (code: {:#x})", hr);
        return hr;
    }
    spdlog::debug(L"The command is: {}", varCommand.bstrVal);

    if (szCurrentDirectory != nullptr)
    {
        // Create the values for the in parameters
        CComVariant varCurrentDirectory = szCurrentDirectory;

        // Store the value for the in parameters
        if (FAILED(hr = pProcessInstance->Put(L"CurrentDirectory", 0, &varCurrentDirectory, 0)))
        {
            spdlog::error(
                "Could not put CurrentDirectory parameter of Create of class Win32_Process (code: {:#x})", hr);
            return hr;
        }

        spdlog::debug(L"The CurrentDirectory is: {}", varCurrentDirectory.bstrVal);
    }

    //************************************
    // Query the Win32_ProcessStartup Class
    CComPtr<IWbemClassObject> pProcessStartupClass;
    CComBSTR bstrProcessStartup(L"Win32_ProcessStartup");
    if (FAILED(hr = m_pServices->GetObject(bstrProcessStartup, 0, NULL, &pProcessStartupClass, NULL)))
    {
        spdlog::error(L"Could not GetObject of class Win32_ProcessStartup");
        return hr;
    }

    // Create a Instance of Win32_ProcessStartup
    CComPtr<IWbemClassObject> pProcessStartupInstance;
    if (FAILED(hr = pProcessStartupClass->SpawnInstance(0, &pProcessStartupInstance)))
    {
        spdlog::error(L"Could not SpawnInstance of class Win32_ProcessStartup");
        return hr;
    }

    CComVariant varCommand_ShowWindow;
    varCommand_ShowWindow = SW_HIDE;
    if (FAILED(hr = pProcessStartupInstance->Put(L"ShowWindow", 0, &varCommand_ShowWindow, 0)))
    {
        spdlog::error("Could not put ShowWindow of class Win32_ProcessStartup");
        return hr;
    }
    spdlog::debug("PriorityClass set to {:#x} in class Win32_ProcessStartup", varCommand_ShowWindow.uintVal);

    if (dwPriority)
    {
        CComVariant varCommand_PriorityClass;
        varCommand_PriorityClass = (UINT32)dwPriority;
        if (FAILED(hr = pProcessStartupInstance->Put(L"PriorityClass", 0, &varCommand_PriorityClass, 0)))
        {
            spdlog::error(
                "Could not put PriorityClass of class Win32_ProcessStartup with {:#x} (code: {:#x})", dwPriority, hr);
            return hr;
        }
        spdlog::debug("PriorityClass set to {:#x} in class Win32_ProcessStartup", dwPriority);
    }

    if (dwCreationFlags)
    {
        CComVariant varCommand_CreateFlags;
        varCommand_CreateFlags = (INT32)dwCreationFlags;
        varCommand.ChangeType(VT_UI4);

        if (FAILED(hr = pProcessStartupInstance->Put(L"CreateFlags", 0, &varCommand_CreateFlags, 0L)))
        {
            spdlog::error("Could not put CreateFlags of class Win32_ProcessStartup {:#x}", dwCreationFlags);
            return hr;
        }
        spdlog::debug("CreateFlags set to {:#x} in class Win32_ProcessStartup", dwCreationFlags);
    }

    // Create a command to set ProcessStartupInformation to be the instance of Win32_ProcessStartup
    CComVariant varCommand_ProcessStartup;
    varCommand_ProcessStartup = pProcessStartupInstance;
    // set the value to the instance of Win32_Process process
    if (FAILED(hr = pProcessInstance->Put(L"ProcessStartupInformation", 0, &varCommand_ProcessStartup, 0)))
    {
        spdlog::error("Could not put ProcessStartupInformation of class Win32_Process (code: {:#x})", hr);
        return hr;
    }

    // Execute Method
    CComPtr<IWbemClassObject> pOutParams;
    if (FAILED(hr = m_pServices->ExecMethod(ClassName, MethodName, 0, NULL, pProcessInstance, &pOutParams, NULL)))
    {
        spdlog::error("Could not put execute command (code: {:#x})", hr);
        return hr;
    }

    // To see what the method returned,
    // use the following code.  The return value will
    // be in &varReturnValue
    CComVariant varReturnValue;
    if (FAILED(hr = pOutParams->Get(CComBSTR(L"ReturnValue"), 0, &varReturnValue, NULL, 0)))
    {
        spdlog::error("Could not retrieve value ReturnValue (code: {:#x})", hr);
        return hr;
    }
    spdlog::debug("Command was successfully created, ReturnValue={}", varReturnValue.uintVal);
    dwStatus = varReturnValue.uintVal;
    return S_OK;
}

stx::Result<CComPtr<IEnumWbemClassObject>, HRESULT> Orc::WMI::Query(LPCWSTR szRequest) const
{
    CComPtr<IEnumWbemClassObject> pEnumerator;
    if (auto hr = m_pServices->ExecQuery(
                bstr_t("WQL"),
                bstr_t(szRequest),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                NULL,
                &pEnumerator); FAILED(hr))
    {
        spdlog::error(L"Query \"{}\" failed (code: {:#x})", szRequest, hr);
        return Err(std::move(hr));
    }

    return Ok(std::move(pEnumerator));
}


template <>
stx::Result<bool,HRESULT> Orc::WMI::GetProperty<bool>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty) 
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_BOOLEAN)
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_BOOL) && FAILED(vtProp.ChangeType(VT_BOOL)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok((bool)vtProp.boolVal);
}

template <>
stx::Result<SHORT, HRESULT> Orc::WMI::GetProperty<SHORT>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_SINT16 )
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_I2) && FAILED(vtProp.ChangeType(VT_I2)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::move((SHORT)vtProp.iVal));
}


template <>
stx::Result<USHORT, HRESULT> Orc::WMI::GetProperty<USHORT>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_UINT16)
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_UI2) && FAILED(vtProp.ChangeType(VT_UI2)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::move((USHORT)vtProp.uiVal));
}

template <>
stx::Result<ULONG32, HRESULT> Orc::WMI::GetProperty<ULONG32>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_UINT32)
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_UI4) && FAILED(vtProp.ChangeType(VT_UI4)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::move((ULONG32)vtProp.ulVal));
}

template <>
stx::Result<LONG32, HRESULT> Orc::WMI::GetProperty<LONG32>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_SINT32 || !(vtProp.vt & VT_I4))
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_I4) && FAILED(vtProp.ChangeType(VT_I4)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::move((LONG32)vtProp.lVal));
}

template <>
stx::Result<ULONG64, HRESULT> Orc::WMI::GetProperty<ULONG64>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));
    if (propType != CIM_UINT64)
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_UI8) && FAILED(vtProp.ChangeType(VT_UI8)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::move((ULONG64)vtProp.ullVal));
}

template <>
stx::Result<LONG64, HRESULT> Orc::WMI::GetProperty<LONG64>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_SINT64)
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_I8) && FAILED(vtProp.ChangeType(VT_I8)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::move((LONG64)vtProp.llVal));
}

template <>
stx::Result<ByteBuffer, HRESULT>
Orc::WMI::GetProperty<ByteBuffer>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    return Err(E_NOTIMPL);
}

template <>
stx::Result<std::wstring, HRESULT>
Orc::WMI::GetProperty<std::wstring>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));

    if (propType != CIM_STRING)
        return Err(E_NOT_VALID_STATE);

    if (!(vtProp.vt & VT_BSTR) && FAILED(vtProp.ChangeType(VT_BSTR)))
        return Err(HRESULT_FROM_WIN32(ERROR_INVALID_VARIANT));

    return Ok(std::wstring(vtProp.bstrVal, SysStringLen(vtProp.bstrVal)));
}


template <>
stx::Result<std::vector<std::wstring>,HRESULT>
Orc::WMI::GetProperty<std::vector<std::wstring>>(const CComPtr<IWbemClassObject>& obj, LPCWSTR szProperty)
{
    CComVariant vtProp;
    CIMTYPE propType;
    if (auto hr = obj->Get(szProperty, 0, &vtProp, &propType, 0); FAILED(hr))
        return Err(std::move(hr));
    if (propType != CIM_FLAG_ARRAY || !(vtProp.vt & (VT_ARRAY|VT_BSTR)))
        return Err(E_NOT_VALID_STATE);
    return Err(E_NOTIMPL);
}

HRESULT WMI::WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives) const
{
    HRESULT hr = E_FAIL;

    auto result = Query(L"SELECT DeviceID FROM Win32_DiskDrive");
    if (result.is_err())
        return result.err_value();

    auto pEnumerator = std::move(result).unwrap();

    ULONG uReturn = 0;

    while (pEnumerator)
    {
        CComPtr<IWbemClassObject> pclsObj;

        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

        if (0 == uReturn)
            break;

        CComVariant vtProp;

        // Get the value of the Name property
        hr = pclsObj->Get(L"DeviceID", 0, &vtProp, 0, 0);

        physicaldrives.push_back(vtProp.bstrVal);
    }

    return S_OK;
}

WMI::~WMI() {}
