//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ChakraExtension.h"

// ChakraCore
#include "chakracommon.h"

using namespace Orc;

HRESULT ChakraExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (!IsLoaded())
    {
        HRESULT hr = E_FAIL;
        if (FAILED(hr = Load()))
        {
            log::Error(_L_, hr, L"Failed to load chakracore dll\r\n");
            return hr;
        }
    }

    if (IsLoaded())
    {
        Get(m_JsCreateRuntime, "JsCreateRuntime");
        Get(m_JsSetCurrentContext, "JsSetCurrentContext");
        Get(m_JsDisposeRuntime, "JsDisposeRuntime");
        Get(m_JsCreateContext, "JsCreateContext");
        Get(m_JsCreateObject, "JsCreateObject");
        Get(m_JsCreateFunction, "JsCreateFunction");
        Get(m_JsCreateError, "JsCreateError");
        Get(m_JsSetException, "JsSetException");
        Get(m_JsSetObjectBeforeCollectCallback, "JsSetObjectBeforeCollectCallback");
        Get(m_JsGetPropertyIdFromName, "JsGetPropertyIdFromName");
        Get(m_JsDefineProperty, "JsDefineProperty");
        Get(m_JsGetExternalData, "JsGetExternalData");
        Get(m_JsPointerToString, "JsPointerToString");
        Get(m_JsDoubleToNumber, "JsDoubleToNumber");
        Get(m_JsIntToNumber, "JsIntToNumber");
        Get(m_JsBoolToBoolean, "JsBoolToBoolean");
        Get(m_JsGetNullValue, "JsGetNullValue");
        Get(m_JsCreateRangeError, "JsCreateRangeError");
        Get(m_JsCreateArray, "JsCreateArray");
        Get(m_JsCreateExternalArrayBuffer, "JsCreateExternalArrayBuffer");
        Get(m_JsCreateTypedArray, "JsCreateTypedArray");
        Get(m_JsGetTrueValue, "JsGetTrueValue");
        Get(m_JsGetFalseValue, "JsGetFalseValue");
        Get(m_JsGetAndClearException, "JsGetAndClearException");
        Get(m_JsGetGlobalObject, "JsGetGlobalObject");
        Get(m_JsSetPrototype, "JsSetPrototype");
        Get(m_JsCreateExternalObject, "JsCreateExternalObject");
        Get(m_JsGetValueType, "JsGetValueType");
        Get(m_JsConvertValueToObject, "JsConvertValueToObject");
        Get(m_JsConvertValueToNumber, "JsConvertValueToNumber");
        Get(m_JsConvertValueToString, "JsConvertValueToString");
        Get(m_JsGetPrototype, "JsGetPrototype");
        Get(m_JsGetIndexedProperty, "JsGetIndexedProperty");
        Get(m_JsSetProperty, "JsSetProperty");
        Get(m_JsCallFunction, "JsCallFunction");
        Get(m_JsGetUndefinedValue, "JsGetUndefinedValue");
        Get(m_JsSetIndexedProperty, "JsSetIndexedProperty");
        Get(m_JsBooleanToBool, "JsBooleanToBool");
        Get(m_JsNumberToInt, "JsNumberToInt");
        Get(m_JsNumberToDouble, "JsNumberToDouble");
        Get(m_JsStringToPointer, "JsStringToPointer");
        Get(m_JsAddRef, "JsAddRef");
        Get(m_JsRelease, "JsRelease");
        Get(m_JsGetProperty, "JsGetProperty");
        Get(m_JsHasException, "JsHasException");
        Get(m_JsRunScript, "JsRunScript");
        Get(m_JsParseScript, "JsParseScript");
        Get(m_JsParseScriptWithAttributes, "JsParseScriptWithAttributes");
        Get(m_JsSetExternalData, "JsSetExternalData");

        m_bInitialized = true;
    }

    return S_OK;
}
