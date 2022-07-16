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

#include "Flags.h"

#include "Text/Fmt/std_error_code.h"

#include <fmt/format.h>

#include <In6addr.h>
#include <inaddr.h>

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;

namespace StructuredOutput {

// declare the caller's interface
class IOutput
{
public:
    virtual HRESULT BeginElement(LPCWSTR szElement) PURE;
    virtual HRESULT EndElement(LPCWSTR szElement) PURE;

    virtual HRESULT BeginCollection(LPCWSTR szCollection) PURE;
    virtual HRESULT EndCollection(LPCWSTR szCollection) PURE;

    virtual HRESULT Write(LPCWSTR szString) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, LPCWSTR szValue) PURE;

    virtual HRESULT Write(const std::wstring_view str) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, const std::wstring_view str) PURE;

    virtual HRESULT Write(const std::wstring& str) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, const std::wstring& str) PURE;

    virtual HRESULT Write(const std::string_view str) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, const std::string_view str) PURE;

    virtual HRESULT Write(bool bBoolean) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, bool bBoolean) PURE;

    virtual HRESULT Write(uint32_t dwValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, uint32_t dwValue, bool bInHex = false) PURE;

    virtual HRESULT Write(unsigned long dwValue, bool bInHex = false)
    {
        return Write(static_cast<uint32_t>(dwValue), bInHex);
    }

    virtual HRESULT WriteNamed(LPCWSTR szName, unsigned long dwValue, bool bInHex = false)
    {
        return WriteNamed(szName, static_cast<uint32_t>(dwValue), bInHex);
    }

    virtual HRESULT Write(long dwValue, bool bInHex = false) { return Write(static_cast<int32_t>(dwValue), bInHex); }

    virtual HRESULT WriteNamed(LPCWSTR szName, long dwValue, bool bInHex = false)
    {
        return WriteNamed(szName, static_cast<int32_t>(dwValue), bInHex);
    }

    virtual HRESULT Write(int32_t dwValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, int32_t dwValue, bool bInHex = false) PURE;

    virtual HRESULT Write(uint64_t ullValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, uint64_t ullValue, bool bInHex = false) PURE;

    virtual HRESULT Write(int64_t ullValue, bool bInHex = false) PURE;
    virtual HRESULT WriteNamed(LPCWSTR szName, int64_t ullValue, bool bInHex = false) PURE;

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

    template <typename _T>
    HRESULT Write(Result<_T> arg, std::wstring_view message = {})
    {
        using namespace std::string_view_literals;
        if (arg.has_value())
            return Write(arg.value());
        else if (!message.empty())
            Log::Warn("{}: {}"sv, message, arg.error());
        else
            Log::Warn("Result in error: {}"sv, arg.error());
        return S_OK;
    }

    template <typename _T>
    HRESULT WriteNamed(LPCWSTR szName, Result<_T> arg, std::wstring_view message = {})
    {
        using namespace std::string_view_literals;
        if (arg.has_value())
            return WriteNamed(szName, arg.value());
        else if (!message.empty())
            Log::Warn(L"{}: {}"sv, message, arg.error());
        else
            Log::Warn(L"Result in error: {}"sv, arg.error());
        return S_OK;
    }

    HRESULT WriteNamedFileTime(LPCWSTR szName, Result<ULONGLONG> filetime, std::wstring_view message = {})
    {
        using namespace std::string_view_literals;
        if (filetime.has_value())
            return WriteNamedFileTime(szName, filetime.value());
        else if (!message.empty())
            Log::Warn(L"{}: {}"sv, message, filetime.error());
        else
            Log::Warn(L"Result in error: {}"sv, filetime.error());
        return S_OK;
    }

    template <typename... Args>
    HRESULT WriteFormated(const std::wstring_view& szFormat, Args&&... args)
    {
        return WriteFormated_(szFormat, fmt::make_format_args<fmt::wformat_context>(args...));
    }

    template <typename... Args>
    HRESULT WriteNamedFormated(LPCWSTR szName, const std::wstring_view& szFormat, Args&&... args)
    {
        return WriteNamedFormated_(szName, szFormat, fmt::make_format_args<fmt::wformat_context>(args...));
    }

    template <typename... Args>
    HRESULT WriteFormated(const std::string_view& szFormat, Args&&... args)
    {
        return WriteFormated_(szFormat, fmt::make_format_args(args...));
    }

    template <typename... Args>
    HRESULT WriteNamedFormated(LPCWSTR szName, const std::string_view& szFormat, Args&&... args)
    {
        return WriteNamedFormated_(szName, szFormat, fmt::make_format_args(args...));
    }

protected:
    virtual HRESULT WriteFormated_(const std::string_view& szFormat, fmt::format_args args) PURE;
    virtual HRESULT WriteFormated_(const std::wstring_view& szFormat, fmt::wformat_args args) PURE;
    virtual HRESULT WriteNamedFormated_(LPCWSTR szName, const std::string_view& szFormat, fmt::format_args args) PURE;
    virtual HRESULT WriteNamedFormated_(LPCWSTR szName, const std::wstring_view& szFormat, fmt::wformat_args args) PURE;
};

class IWriter : public IOutput
{
public:
    using Ptr = std::shared_ptr<IWriter>;

    virtual HRESULT Close() PURE;
};

class ScopedRootElement : private resource_scope<IWriter>
{
public:
    ScopedRootElement(IWriter& writer, LPCWSTR szName)
        : resource_scope(
            writer,
            [&](IWriter& writer) { writer.BeginElement(szName); },
            [&](IWriter& writer) {
                writer.EndElement(szName);
                writer.Close();
            })
    {
    }
};

class ScopedElement : private resource_scope<IOutput>
{
public:
    ScopedElement(IOutput& writer, LPCWSTR szName)
        : resource_scope(
            writer,
            [&](IOutput& writer) { writer.BeginElement(szName); },
            [&](IOutput& writer) { writer.EndElement(szName); })
    {
    }
};

class ScopedCollection : private resource_scope<IOutput>
{
public:
    ScopedCollection(IOutput& writer, LPCWSTR szName)
        : resource_scope(
            writer,
            [&](IOutput& writer) { writer.BeginCollection(szName); },
            [&](IOutput& writer) { writer.EndCollection(szName); })
    {
    }
};

}  // namespace StructuredOutput

using IStructuredOutput = StructuredOutput::IOutput;
using IStructuredWriter = StructuredOutput::IWriter;

}  // namespace Orc
