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
#include "OutputSpec.h"

#include <Windows.h>
#include <In6addr.h>
#include <inaddr.h>
#include <ObjIdl.h>


#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;
class LogFileWriter;

namespace StructuredOutput {

struct ORCLIB_API Options : public Orc::OutputOptions
{
    OutputSpec::Encoding Encoding = OutputSpec::Encoding::UTF8;
};

namespace JSON {
struct ORCLIB_API Options : public StructuredOutput::Options
{
    bool bPrettyPrint = false;
    DWORD indentCharCount = 4;
};
}  // namespace JSON

namespace XML {
struct ORCLIB_API Options : public StructuredOutput::Options
{
};
}

using JSONOutputOptions = JSON::Options;
using XMLOutputOptions  = XML::Options;

   // declare the caller's interface
class IWriter
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

    virtual HRESULT Close() PURE;
};

class LegacyWriter : public IWriter
{
public:
    template <typename... Args>
    HRESULT WriteNameFormatedStringPair(LPCWSTR szName, const WCHAR* szFormat, Args&&... args)
    {
        return WriteNamedFormated(szName, szFormat, std::forward<Args>(args)...);
    }
    inline HRESULT WriteNameValuePair(LPCWSTR szName, const WCHAR* szValue) { return WriteNamed(szName, szValue); }

    inline HRESULT WriteNameValuePair(LPCWSTR szName, ULONG32 dwValue, bool bInHex = false)
    {
        return WriteNamed(szName, dwValue, bInHex);
    }
    inline HRESULT WriteNameValuePair(LPCWSTR szName, ULONG64 ullValue, bool bInHex = false)
    {
        return WriteNamed(szName, ullValue, bInHex);
    }

    inline HRESULT WriteNameValuePair(LPCWSTR szName, LONG32 dwValue, bool bInHex = false)
    {
        return WriteNamed(szName, dwValue, bInHex);
    }
    inline HRESULT WriteNameValuePair(LPCWSTR szName, LONG64 ullValue, bool bInHex = false)
    {
        return WriteNamed(szName, ullValue, bInHex);
    }

    inline HRESULT WriteNameValuePair(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false)
    {
        return WriteNamed(szName, ullValue, bInHex);
    }
    inline HRESULT WriteNameSizePair(LPCWSTR szName, size_t ullValue, bool bInHex = false)
    {
        return WriteNamed(szName, (ULONGLONG) ullValue, bInHex);
    }
    inline HRESULT WriteNameAttributesPair(LPCWSTR szName, DWORD dwAttibutes)
    {
        return WriteNamedAttributes(szName, dwAttibutes);
    }
    inline HRESULT WriteNameFileTimePair(LPCWSTR szName, FILETIME fileTime) { return WriteNamed(szName, fileTime); }
    inline HRESULT WriteNameFileTimePair(LPCWSTR szName, LONGLONG fileTime)
    {
        ULARGE_INTEGER li;
        li.QuadPart = fileTime;
        FILETIME ft {li.LowPart, li.HighPart};
        return WriteNamed(szName, ft);
    }
    inline HRESULT WriteNameCharArrayPair(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount)
    {
        return WriteNamed(szName, szArray, dwCharCount);
    }
    inline HRESULT WriteNameBytesInHexPair(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix)
    {
        return WriteNamed(szName, pSHA1, dwLen, b0xPrefix);
    }
    inline HRESULT WriteNameBytesInHexPair(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix)
    {
        return WriteNamed(szName, Buffer, b0xPrefix);
    }
    inline HRESULT WriteNameBoolPair(LPCWSTR szName, bool bBoolean) { return WriteNamed(szName, bBoolean); }
    inline HRESULT WriteNameEnumPair(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[])
    {
        return WriteNamed(szName, dwEnum, EnumValues);
    }
    inline HRESULT
    WriteNameFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
    {
        return WriteNamed(szName, dwFlags, FlagValues, cSeparator);
    }
    inline HRESULT WriteNameExactFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[])
    {
        return WriteNamed(szName, dwFlags, FlagValues);
    }
    inline HRESULT WriteNameIPPair(LPCWSTR szName, IN_ADDR& ip) { return WriteNamed(szName, ip); }
    inline HRESULT WriteNameIPPair(LPCWSTR szName, IN6_ADDR& ip) { return WriteNamed(szName, ip); }
    inline HRESULT WriteNameGUIDPair(LPCWSTR szName, const GUID& guid) { return WriteNamed(szName, guid); }
    inline HRESULT WriteString(LPCWSTR szString) { return Write(szString); }
};

class ORCLIB_API Writer : public Orc::OutputWriter, public LegacyWriter
{

public:

    using _Buffer = Buffer<WCHAR, 128>;

    Writer(Orc::logger pLog, std::unique_ptr<Options>&& pOptions)
        : _L_(std::move(pLog))
        , m_Options(std::move(pOptions))
    {
    }

    static std::shared_ptr<IWriter> GetWriter(
        const logger& pLog,
        std::shared_ptr<ByteStream> stream,
        OutputSpec::Kind kindOfFile,
        std::unique_ptr<Options>&& pOptions);
    static std::shared_ptr<IWriter>
    GetWriter(const logger& pLog, const OutputSpec& outFile, std::unique_ptr<Options>&& pOptions);
    static std::shared_ptr<IWriter> GetWriter(
        const logger& pLog,
        const OutputSpec& outFile,
        const std::wstring& strPattern,
        const std::wstring& strName,
        std::unique_ptr<Options>&& pOptions);

protected:
    
    HRESULT WriteBuffer(_Buffer& buffer, ULONG32 dwValue, bool bInHex = false);
    HRESULT WriteBuffer(_Buffer& buffer, ULONG64 ullValue, bool bInHex = false);
    HRESULT WriteBuffer(_Buffer& buffer, LONG32 dwValue, bool bInHex = false);
    HRESULT WriteBuffer(_Buffer& buffer, LONG64 ullValue, bool bInHex = false);
    HRESULT WriteBuffer(_Buffer& buffer, LARGE_INTEGER ullValue, bool bInHex = false);

    HRESULT WriteAttributesBuffer(_Buffer& buffer, DWORD dwAttibutes);
    HRESULT WriteBuffer(_Buffer& buffer, FILETIME fileTime);
    HRESULT WriteBuffer(_Buffer& buffer, const WCHAR* szArray, DWORD dwCharCount);
    HRESULT WriteBuffer(_Buffer& buffer, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix);
    HRESULT WriteBuffer(_Buffer& buffer, const CBinaryBuffer& Buffer, bool b0xPrefix);

    HRESULT WriteBuffer(_Buffer& buffer, DWORD dwFlags, const FlagsDefinition FlagValues[]);
    HRESULT WriteBuffer(_Buffer& buffer, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator);

    HRESULT WriteBuffer(_Buffer& buffer, IN_ADDR& ip);

protected:
    Orc::logger _L_;
    std::unique_ptr<Options> m_Options;
};

}  // namespace StrucuturedOutput

using StructuredOutputOptions = StructuredOutput::Options;
using StructuredOutputWriter = StructuredOutput::Writer;

}  // namespace Orc

#pragma managed(pop)
