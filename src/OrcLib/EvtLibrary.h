//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ExtensionLibrary.h"

#include <windows.h>
#include <functional>
#include <winternl.h>

#pragma managed(push, off)

namespace Orc {

using EVT_HANDLE = HANDLE;
using PEVT_HANDLE = HANDLE*;

enum EVT_VARIANT_TYPE
{
    EvtVarTypeNull = 0,
    EvtVarTypeString = 1,
    EvtVarTypeAnsiString = 2,
    EvtVarTypeSByte = 3,
    EvtVarTypeByte = 4,
    EvtVarTypeInt16 = 5,
    EvtVarTypeUInt16 = 6,
    EvtVarTypeInt32 = 7,
    EvtVarTypeUInt32 = 8,
    EvtVarTypeInt64 = 9,
    EvtVarTypeUInt64 = 10,
    EvtVarTypeSingle = 11,
    EvtVarTypeDouble = 12,
    EvtVarTypeBoolean = 13,
    EvtVarTypeBinary = 14,
    EvtVarTypeGuid = 15,
    EvtVarTypeSizeT = 16,
    EvtVarTypeFileTime = 17,
    EvtVarTypeSysTime = 18,
    EvtVarTypeSid = 19,
    EvtVarTypeHexInt32 = 20,
    EvtVarTypeHexInt64 = 21,

    // these types used internally
    EvtVarTypeEvtHandle = 32,
    EvtVarTypeEvtXml = 35

};

static const auto EVT_VARIANT_TYPE_MASK = 0x7f;
static const auto EVT_VARIANT_TYPE_ARRAY = 128;

typedef struct _EVT_VARIANT
{
    union
    {
        BOOL BooleanVal;
        INT8 SByteVal;
        INT16 Int16Val;
        INT32 Int32Val;
        INT64 Int64Val;
        UINT8 ByteVal;
        UINT16 UInt16Val;
        UINT32 UInt32Val;
        ULONG64 UInt64Val;
        float SingleVal;
        double DoubleVal;
        ULONGLONG FileTimeVal;
        SYSTEMTIME* SysTimeVal;
        GUID* GuidVal;
        LPCWSTR StringVal;
        LPCSTR AnsiStringVal;
        PBYTE BinaryVal;
        PSID SidVal;
        size_t SizeTVal;

        // array fields
        BOOL* BooleanArr;
        INT8* SByteArr;
        INT16* Int16Arr;
        INT32* Int32Arr;
        INT64* Int64Arr;
        UINT8* ByteArr;
        UINT16* UInt16Arr;
        UINT32* UInt32Arr;
        ULONG64* UInt64Arr;
        float* SingleArr;
        double* DoubleArr;
        FILETIME* FileTimeArr;
        SYSTEMTIME* SysTimeArr;
        GUID* GuidArr;
        LPWSTR* StringArr;
        LPSTR* AnsiStringArr;
        PSID* SidArr;
        size_t* SizeTArr;

        // internal fields
        EVT_HANDLE EvtHandleVal;
        LPCWSTR XmlVal;
        LPCWSTR* XmlValArr;
    };

    DWORD Count;  // number of elements (not length) in bytes.
    DWORD Type;

} EVT_VARIANT, *PEVT_VARIANT;

typedef enum _EVT_QUERY_FLAGS
{
    EvtQueryChannelPath = 0x1,
    EvtQueryFilePath = 0x2,
    EvtQueryForwardDirection = 0x100,
    EvtQueryReverseDirection = 0x200,
    EvtQueryTolerateQueryErrors = 0x1000
} EVT_QUERY_FLAGS;

////////////////////////////////////////////////////////////////////////////////
//
// Rendering
//
////////////////////////////////////////////////////////////////////////////////

typedef enum _EVT_SYSTEM_PROPERTY_ID
{
    EvtSystemProviderName = 0,  // EvtVarTypeString
    EvtSystemProviderGuid,  // EvtVarTypeGuid
    EvtSystemEventID,  // EvtVarTypeUInt16
    EvtSystemQualifiers,  // EvtVarTypeUInt16
    EvtSystemLevel,  // EvtVarTypeUInt8
    EvtSystemTask,  // EvtVarTypeUInt16
    EvtSystemOpcode,  // EvtVarTypeUInt8
    EvtSystemKeywords,  // EvtVarTypeHexInt64
    EvtSystemTimeCreated,  // EvtVarTypeFileTime
    EvtSystemEventRecordId,  // EvtVarTypeUInt64
    EvtSystemActivityID,  // EvtVarTypeGuid
    EvtSystemRelatedActivityID,  // EvtVarTypeGuid
    EvtSystemProcessID,  // EvtVarTypeUInt32
    EvtSystemThreadID,  // EvtVarTypeUInt32
    EvtSystemChannel,  // EvtVarTypeString
    EvtSystemComputer,  // EvtVarTypeString
    EvtSystemUserID,  // EvtVarTypeSid
    EvtSystemVersion,  // EvtVarTypeUInt8
    EvtSystemPropertyIdEND

} EVT_SYSTEM_PROPERTY_ID;

typedef enum _EVT_RENDER_CONTEXT_FLAGS
{
    EvtRenderContextValues = 0,  // Render specific properties
    EvtRenderContextSystem,  // Render all system properties (System)
    EvtRenderContextUser  // Render all user properties (User/EventData)

} EVT_RENDER_CONTEXT_FLAGS;

typedef enum _EVT_RENDER_FLAGS
{
    EvtRenderEventValues = 0,  // Variants
    EvtRenderEventXml,  // XML
    EvtRenderBookmark  // Bookmark

} EVT_RENDER_FLAGS;

class EvtLibrary : public ExtensionLibrary
{
    friend class ExtensionLibrary;

public:
    EvtLibrary()
        : ExtensionLibrary(L"wevtapi", L"Wevtapi.dll") {};

    STDMETHOD(Initialize)();

    template <typename... Args>
    auto EvtQuery(Args&&... args)
    {
        return Call(m_EvtQuery, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto EvtNext(Args&&... args)
    {
        return Call(m_EvtNext, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto EvtCreateRenderContext(Args&&... args)
    {
        return Call(m_EvtCreateRenderContext, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto EvtRender(Args&&... args)
    {
        return Call(m_EvtRender, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto EvtClose(Args&&... args)
    {
        return Call(m_EvtClose, std::forward<Args>(args)...);
    }

    ~EvtLibrary();

private:
    EVT_HANDLE(WINAPI* m_EvtQuery)
    (_In_ EVT_HANDLE Session, _In_ LPCWSTR Path, _In_ LPCWSTR Query, _In_ DWORD Flags) = nullptr;
    BOOL(WINAPI* m_EvtNext)
    (_In_ EVT_HANDLE ResultSet,
     _In_ DWORD EventArraySize,
     _In_ EVT_HANDLE* EventArray,
     _In_ DWORD Timeout,
     _In_ DWORD Flags,
     _Out_ PDWORD Returned) = nullptr;
    EVT_HANDLE(WINAPI* m_EvtCreateRenderContext)
    (_In_ DWORD ValuePathsCount, _In_ LPCWSTR* ValuePaths, _In_ DWORD Flags) = nullptr;

    BOOL(WINAPI* m_EvtRender)
    (_In_ EVT_HANDLE Context,
     _In_ EVT_HANDLE Fragment,
     _In_ DWORD Flags,
     _In_ DWORD BufferSize,
     _In_ PVOID Buffer,
     _Out_ PDWORD BufferUsed,
     _Out_ PDWORD PropertyCount) = nullptr;
    BOOL(WINAPI* m_EvtClose)(_In_ EVT_HANDLE Object) = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
