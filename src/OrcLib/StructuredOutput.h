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

#include <In6addr.h>
#include <inaddr.h>

#pragma managed(push, off)

namespace Orc {

    class CBinaryBuffer;
    class LogFileWriter;

namespace StructuredOutput {

    // declare the caller's interface
    class IOutput
    {
    public:
        virtual HRESULT BeginElement(LPCWSTR szElement) PURE;
        virtual HRESULT EndElement(LPCWSTR szElement) PURE;

        virtual HRESULT BeginCollection(LPCWSTR szCollection) PURE;
        virtual HRESULT EndCollection(LPCWSTR szCollection) PURE;

        virtual HRESULT WriteFormated(const WCHAR* szFormat, ...) PURE;
        virtual HRESULT WriteNamedFormated(LPCWSTR szName, const WCHAR* szFormat, ...) PURE;

        virtual HRESULT Write(LPCWSTR szString) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, LPCWSTR szValue) PURE;

        virtual HRESULT Write(bool bBoolean) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, bool bBoolean) PURE;

        virtual HRESULT Write(ULONG32 dwValue, bool bInHex = false) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, ULONG32 dwValue, bool bInHex = false) PURE;

        virtual HRESULT Write(LONG32 dwValue, bool bInHex = false) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, LONG32 dwValue, bool bInHex = false) PURE;

        virtual HRESULT Write(ULONG64 ullValue, bool bInHex = false) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, ULONG64 ullValue, bool bInHex = false) PURE;

        virtual HRESULT Write(LONG64 ullValue, bool bInHex = false) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, LONG64 ullValue, bool bInHex = false) PURE;

        virtual HRESULT Write(LARGE_INTEGER ullValue, bool bInHex = false) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false) PURE;

        virtual HRESULT WriteAttributes(DWORD dwAttibutes) PURE;
        virtual HRESULT WriteNamedAttributes(LPCWSTR szName, DWORD dwAttibutes) PURE;

        virtual HRESULT Write(FILETIME fileTime) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, FILETIME fileTime) PURE;

        virtual HRESULT WriteFileTime(ULONGLONG fileTime) PURE;
        virtual HRESULT WriteNamedFileTime(LPCWSTR szName, ULONGLONG fileTime) PURE;

        virtual HRESULT Write(const WCHAR* szArray, DWORD dwCharCount) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount) PURE;

        virtual HRESULT Write(const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) PURE;

        virtual HRESULT Write(const CBinaryBuffer& Buffer, bool b0xPrefix) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix) PURE;

        virtual HRESULT Write(DWORD dwEnum, const WCHAR* EnumValues[]) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[]) PURE;

        virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) PURE;
        virtual HRESULT
        WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) PURE;

        virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[]) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[]) PURE;

        virtual HRESULT Write(IN_ADDR& ip) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, IN_ADDR& ip) PURE;

        virtual HRESULT Write(IN6_ADDR& ip) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, IN6_ADDR& ip) PURE;

        virtual HRESULT Write(const GUID& guid) PURE;
        virtual HRESULT WriteNamed(LPCWSTR szName, const GUID& guid) PURE;

        virtual HRESULT WriteComment(LPCWSTR szComment) PURE;

    };

    class IWriter : public IOutput
    {
    public:
        virtual HRESULT Close() PURE;
    };

}  // namespace StructuredOutput

using IStructuredOutput = StructuredOutput::IOutput;
using IStructuredWriter = StructuredOutput::IWriter;

}  // namespace Orc
