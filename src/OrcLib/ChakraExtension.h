//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#include "ChakraCommon.h"

#pragma managed(push, off)

namespace Orc {

using namespace std::string_literals;

class ChakraExtension : public ExtensionLibrary
{
    friend class ExtensionLibrary;

    JsErrorCode(__stdcall* m_JsCreateRuntime)(
        _In_ JsRuntimeAttributes attributes,
        _In_opt_ JsThreadServiceCallback threadService,
        _Out_ JsRuntimeHandle* runtime) = nullptr;
    JsErrorCode(__stdcall* m_JsSetCurrentContext)(_In_ JsContextRef context) = nullptr;
    JsErrorCode(__stdcall* m_JsDisposeRuntime)(_In_ JsRuntimeHandle runtime) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateContext)(_In_ JsRuntimeHandle runtime, _Out_ JsContextRef* newContext) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateObject)(_Out_ JsValueRef* object) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateFunction)(
        _In_ JsNativeFunction nativeFunction,
        _In_opt_ void* callbackState,
        _Out_ JsValueRef* function) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateError)(_In_ JsValueRef message, _Out_ JsValueRef* error) = nullptr;
    JsErrorCode(__stdcall* m_JsSetException)(_In_ JsValueRef exception) = nullptr;
    JsErrorCode(__stdcall* m_JsSetObjectBeforeCollectCallback)(
        _In_ JsRef ref,
        _In_opt_ void* callbackState,
        _In_ JsObjectBeforeCollectCallback objectBeforeCollectCallback) = nullptr;
    JsErrorCode(__stdcall* m_JsGetPropertyIdFromName)(_In_z_ const wchar_t* name, _Out_ JsPropertyIdRef* propertyId) =
        nullptr;
    JsErrorCode(__stdcall* m_JsDefineProperty)(
        _In_ JsValueRef object,
        _In_ JsPropertyIdRef propertyId,
        _In_ JsValueRef propertyDescriptor,
        _Out_ bool* result) = nullptr;
    JsErrorCode(__stdcall* m_JsGetExternalData)(_In_ JsValueRef object, _Out_ void** externalData) = nullptr;
    JsErrorCode(__stdcall* m_JsPointerToString)(
        _In_reads_(stringLength) const wchar_t* stringValue,
        _In_ size_t stringLength,
        _Out_ JsValueRef* value) = nullptr;
    JsErrorCode(__stdcall* m_JsDoubleToNumber)(_In_ double doubleValue, _Out_ JsValueRef* value) = nullptr;
    JsErrorCode(__stdcall* m_JsIntToNumber)(_In_ int intValue, _Out_ JsValueRef* value) = nullptr;
    JsErrorCode(__stdcall* m_JsBoolToBoolean)(_In_ bool value, _Out_ JsValueRef* booleanValue) = nullptr;
    JsErrorCode(__stdcall* m_JsGetNullValue)(_Out_ JsValueRef* nullValue) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateRangeError)(_In_ JsValueRef message, _Out_ JsValueRef* error) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateArray)(_In_ unsigned int length, _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateExternalArrayBuffer)(
        _Pre_maybenull_ _Pre_writable_byte_size_(byteLength) void* data,
        _In_ unsigned int byteLength,
        _In_opt_ JsFinalizeCallback finalizeCallback,
        _In_opt_ void* callbackState,
        _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateTypedArray)(
        _In_ JsTypedArrayType arrayType,
        _In_ JsValueRef baseArray,
        _In_ unsigned int byteOffset,
        _In_ unsigned int elementLength,
        _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsGetTrueValue)(_Out_ JsValueRef* trueValue) = nullptr;
    JsErrorCode(__stdcall* m_JsGetFalseValue)(_Out_ JsValueRef* falseValue) = nullptr;
    JsErrorCode(__stdcall* m_JsGetAndClearException)(_Out_ JsValueRef* exception) = nullptr;
    JsErrorCode(__stdcall* m_JsGetGlobalObject)(_Out_ JsValueRef* globalObject) = nullptr;
    JsErrorCode(__stdcall* m_JsSetPrototype)(_In_ JsValueRef object, _In_ JsValueRef prototypeObject) = nullptr;
    JsErrorCode(__stdcall* m_JsCreateExternalObject)(
        _In_opt_ void* data,
        _In_opt_ JsFinalizeCallback finalizeCallback,
        _Out_ JsValueRef* object) = nullptr;
    JsErrorCode(__stdcall* m_JsGetValueType)(_In_ JsValueRef value, _Out_ JsValueType* type) = nullptr;
    JsErrorCode(__stdcall* m_JsConvertValueToObject)(_In_ JsValueRef value, _Out_ JsValueRef* object) = nullptr;
    JsErrorCode(__stdcall* m_JsConvertValueToNumber)(_In_ JsValueRef value, _Out_ JsValueRef* numberValue) = nullptr;
    JsErrorCode(__stdcall* m_JsConvertValueToString)(_In_ JsValueRef value, _Out_ JsValueRef* stringValue) = nullptr;
    JsErrorCode(__stdcall* m_JsGetPrototype)(_In_ JsValueRef object, _Out_ JsValueRef* prototypeObject) = nullptr;
    JsErrorCode(__stdcall* m_JsGetIndexedProperty)(
        _In_ JsValueRef object,
        _In_ JsValueRef index,
        _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsSetProperty)(
        _In_ JsValueRef object,
        _In_ JsPropertyIdRef propertyId,
        _In_ JsValueRef value,
        _In_ bool useStrictRules) = nullptr;
    JsErrorCode(__stdcall* m_JsCallFunction)(
        _In_ JsValueRef function,
        _In_reads_(argumentCount) JsValueRef* arguments,
        _In_ unsigned short argumentCount,
        _Out_opt_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsGetUndefinedValue)(_Out_ JsValueRef* undefinedValue) = nullptr;
    JsErrorCode(__stdcall* m_JsSetIndexedProperty)(
        _In_ JsValueRef object,
        _In_ JsValueRef index,
        _In_ JsValueRef value) = nullptr;
    JsErrorCode(__stdcall* m_JsBooleanToBool)(_In_ JsValueRef value, _Out_ bool* boolValue) = nullptr;
    JsErrorCode(__stdcall* m_JsNumberToInt)(_In_ JsValueRef value, _Out_ int* asInt) = nullptr;
    JsErrorCode(__stdcall* m_JsNumberToDouble)(_In_ JsValueRef value, _Out_ double* asDouble) = nullptr;
    JsErrorCode(__stdcall* m_JsStringToPointer)(
        _In_ JsValueRef stringValue,
        _Outptr_result_buffer_(*stringLength) const wchar_t** stringPtr,
        _Out_ size_t* stringLength) = nullptr;
    JsErrorCode(__stdcall* m_JsAddRef)(_In_ JsRef ref, _Out_opt_ unsigned int* count) = nullptr;
    JsErrorCode(__stdcall* m_JsRelease)(_In_ JsRef ref, _Out_opt_ unsigned int* count) = nullptr;
    JsErrorCode(__stdcall* m_JsGetProperty)(
        _In_ JsValueRef object,
        _In_ JsPropertyIdRef propertyId,
        _Out_ JsValueRef* value) = nullptr;
    JsErrorCode(__stdcall* m_JsHasException)(_Out_ bool* hasException) = nullptr;
    JsErrorCode(__stdcall* m_JsRunScript)(
        _In_z_ const wchar_t* script,
        _In_ JsSourceContext sourceContext,
        _In_z_ const wchar_t* sourceUrl,
        _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsParseScript)(
        _In_z_ const wchar_t* script,
        _In_ JsSourceContext sourceContext,
        _In_z_ const wchar_t* sourceUrl,
        _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsParseScriptWithAttributes)(
        _In_z_ const wchar_t* script,
        _In_ JsSourceContext sourceContext,
        _In_z_ const wchar_t* sourceUrl,
        _In_ JsParseScriptAttributes parseAttributes,
        _Out_ JsValueRef* result) = nullptr;
    JsErrorCode(__stdcall* m_JsSetExternalData)(_In_ JsValueRef object, _In_opt_ void* externalData) = nullptr;

    template <typename FuncTor, typename... Args>
    JsErrorCode ChakraCall(FuncTor& func, Args&&... args)
    {
        if (!m_bInitialized && FAILED(Initialize()))
            return JsErrorCode::JsErrorNotImplemented;
        return func(std::forward<Args>(args)...);
    }

public:
    ChakraExtension()
        : ExtensionLibrary(L"chakracore.dll"s, L"CHAKRACORE_X86DLL"s, L"CHAKRACORE_X64DLL"s) {};

    template <typename... Args>
    auto JsCreateRuntime(Args&&... args)
    {
        return ChakraCall(m_JsCreateRuntime, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetCurrentContext(Args&&... args)
    {
        return ChakraCall(m_JsSetCurrentContext, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsDisposeRuntime(Args&&... args)
    {
        return ChakraCall(m_JsDisposeRuntime, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateContext(Args&&... args)
    {
        return ChakraCall(m_JsCreateContext, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateObject(Args&&... args)
    {
        return ChakraCall(m_JsCreateObject, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateFunction(Args&&... args)
    {
        return ChakraCall(m_JsCreateFunction, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateError(Args&&... args)
    {
        return ChakraCall(m_JsCreateError, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetException(Args&&... args)
    {
        return ChakraCall(m_JsSetException, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetObjectBeforeCollectCallback(Args&&... args)
    {
        return ChakraCall(m_JsSetObjectBeforeCollectCallback, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetPropertyIdFromName(Args&&... args)
    {
        return ChakraCall(m_JsGetPropertyIdFromName, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsDefineProperty(Args&&... args)
    {
        return ChakraCall(m_JsDefineProperty, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetExternalData(Args&&... args)
    {
        return ChakraCall(m_JsGetExternalData, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsPointerToString(Args&&... args)
    {
        return ChakraCall(m_JsPointerToString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsDoubleToNumber(Args&&... args)
    {
        return ChakraCall(m_JsDoubleToNumber, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsIntToNumber(Args&&... args)
    {
        return ChakraCall(m_JsIntToNumber, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsBoolToBoolean(Args&&... args)
    {
        return ChakraCall(m_JsBoolToBoolean, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetNullValue(Args&&... args)
    {
        return ChakraCall(m_JsGetNullValue, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateRangeError(Args&&... args)
    {
        return ChakraCall(m_JsCreateRangeError, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateArray(Args&&... args)
    {
        return ChakraCall(m_JsCreateArray, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateExternalArrayBuffer(Args&&... args)
    {
        return ChakraCall(m_JsCreateExternalArrayBuffer, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateTypedArray(Args&&... args)
    {
        return ChakraCall(m_JsCreateTypedArray, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetTrueValue(Args&&... args)
    {
        return ChakraCall(m_JsGetTrueValue, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetFalseValue(Args&&... args)
    {
        return ChakraCall(m_JsGetFalseValue, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetAndClearException(Args&&... args)
    {
        return ChakraCall(m_JsGetAndClearException, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetGlobalObject(Args&&... args)
    {
        return ChakraCall(m_JsGetGlobalObject, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetPrototype(Args&&... args)
    {
        return ChakraCall(m_JsSetPrototype, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCreateExternalObject(Args&&... args)
    {
        return ChakraCall(m_JsCreateExternalObject, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetValueType(Args&&... args)
    {
        return ChakraCall(m_JsGetValueType, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsConvertValueToObject(Args&&... args)
    {
        return ChakraCall(m_JsConvertValueToObject, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsConvertValueToNumber(Args&&... args)
    {
        return ChakraCall(m_JsConvertValueToNumber, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsConvertValueToString(Args&&... args)
    {
        return ChakraCall(m_JsConvertValueToString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetPrototype(Args&&... args)
    {
        return ChakraCall(m_JsGetPrototype, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetIndexedProperty(Args&&... args)
    {
        return ChakraCall(m_JsGetIndexedProperty, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetProperty(Args&&... args)
    {
        return ChakraCall(m_JsSetProperty, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsCallFunction(Args&&... args)
    {
        return ChakraCall(m_JsCallFunction, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetUndefinedValue(Args&&... args)
    {
        return ChakraCall(m_JsGetUndefinedValue, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetIndexedProperty(Args&&... args)
    {
        return ChakraCall(m_JsSetIndexedProperty, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsBooleanToBool(Args&&... args)
    {
        return ChakraCall(m_JsBooleanToBool, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsNumberToInt(Args&&... args)
    {
        return ChakraCall(m_JsNumberToInt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsNumberToDouble(Args&&... args)
    {
        return ChakraCall(m_JsNumberToDouble, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsStringToPointer(Args&&... args)
    {
        return ChakraCall(m_JsStringToPointer, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsAddRef(Args&&... args)
    {
        return ChakraCall(m_JsAddRef, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsRelease(Args&&... args)
    {
        return ChakraCall(m_JsRelease, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsGetProperty(Args&&... args)
    {
        return ChakraCall(m_JsGetProperty, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsHasException(Args&&... args)
    {
        return ChakraCall(m_JsHasException, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsRunScript(Args&&... args)
    {
        return ChakraCall(m_JsRunScript, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsParseScript(Args&&... args)
    {
        return ChakraCall(m_JsParseScript, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsParseScriptWithAttributes(Args&&... args)
    {
        return ChakraCall(m_JsParseScriptWithAttributes, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto JsSetExternalData(Args&&... args)
    {
        return ChakraCall(m_JsSetExternalData, std::forward<Args>(args)...);
    }

    STDMETHOD(Initialize)();

    ~ChakraExtension() {};
};

}  // namespace Orc

#pragma managed(pop)
