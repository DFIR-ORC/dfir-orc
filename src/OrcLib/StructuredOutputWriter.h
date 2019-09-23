//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OutputWriter.h"

#include <Windows.h>
#include <In6addr.h>
#include <inaddr.h>
#include <ObjIdl.h>

#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;
class LogFileWriter;

class ORCLIB_API StructuredOutputWriter : public OutputWriter
{

public:
    virtual HRESULT Close() PURE;

    virtual HRESULT BeginElement(LPCWSTR szElement) PURE;
    virtual HRESULT EndElement(LPCWSTR szElement) PURE;

    virtual HRESULT BeginCollection(LPCWSTR szCollection) PURE;
    virtual HRESULT EndCollection(LPCWSTR szCollection) PURE;

    virtual HRESULT WriteNameValuePair(LPCWSTR szName, LPCWSTR szValue) PURE;
    virtual HRESULT WriteNameFormatedStringPair(LPCWSTR szName, const WCHAR* szFormat, ...) PURE;

    virtual HRESULT WriteNameValuePair(LPCWSTR szName, DWORD dwValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNameValuePair(LPCWSTR szName, ULONGLONG ullValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNameValuePair(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNameSizePair(LPCWSTR szName, size_t ullValue, bool bInHex = false) PURE;

    virtual HRESULT WriteNameAttributesPair(LPCWSTR szName, DWORD dwAttibutes) PURE;

    virtual HRESULT WriteNameFileTimePair(LPCWSTR szName, FILETIME fileTime) PURE;
    virtual HRESULT WriteNameFileTimePair(LPCWSTR szName, LONGLONG fileTime) PURE;

    virtual HRESULT WriteNameCharArrayPair(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount) PURE;

    virtual HRESULT WriteNameBytesInHexPair(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) PURE;
    virtual HRESULT WriteNameBytesInHexPair(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix) PURE;

    virtual HRESULT WriteNameBoolPair(LPCWSTR szName, bool bBoolean) PURE;

    virtual HRESULT WriteNameEnumPair(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[]) PURE;
    virtual HRESULT
    WriteNameFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) PURE;
    virtual HRESULT WriteNameExactFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[]) PURE;

    virtual HRESULT WriteNameIPPair(LPCWSTR szName, IN_ADDR& ip) PURE;
    virtual HRESULT WriteNameIPPair(LPCWSTR szName, IN6_ADDR& ip) PURE;

    virtual HRESULT WriteNameGUIDPair(LPCWSTR szName, const GUID& guid) PURE;

    virtual HRESULT WriteComment(LPCWSTR szComment) PURE;

    virtual HRESULT WriteString(LPCWSTR szString) PURE;

    static std::shared_ptr<StructuredOutputWriter>
    GetWriter(const logger& pLog, const std::shared_ptr<ByteStream>& stream, OutputSpec::Encoding encoding);
    static std::shared_ptr<StructuredOutputWriter> GetWriter(const logger& pLog, const OutputSpec& outFile);
    static std::shared_ptr<StructuredOutputWriter> GetWriter(
        const logger& pLog,
        const OutputSpec& outFile,
        const std::wstring& strPattern,
        const std::wstring& strName);
};

}  // namespace Orc

#pragma managed(pop)
