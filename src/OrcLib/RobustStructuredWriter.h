//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OutputSpec.h"

#include "StructuredOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

class RobustStructuredOptions : public StructuredOutputOptions
{
};

class RobustStructuredWriter : public StructuredOutputWriter
{
protected:
    const std::shared_ptr<StructuredOutputWriter> m_pWriter;

public:
    RobustStructuredWriter(
        const std::shared_ptr<StructuredOutputWriter>& pWriter,
        std::unique_ptr<RobustStructuredOptions> pOptions = nullptr)
        : StructuredOutputWriter(std::move(pOptions))
        , m_pWriter(pWriter) {};

    virtual HRESULT Close() override final;

    virtual HRESULT BeginElement(LPCWSTR szElement) override final;
    virtual HRESULT EndElement(LPCWSTR szElement) override final;

    virtual HRESULT BeginCollection(LPCWSTR szCollection) override final;
    virtual HRESULT EndCollection(LPCWSTR szCollection) override final;

    virtual HRESULT Write(LPCWSTR szString) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const WCHAR* szValue) override final;

    virtual HRESULT Write(const std::wstring_view str) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const std::wstring_view str) override final;

    virtual HRESULT Write(const std::wstring& str) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const std::wstring& str) override final;

    virtual HRESULT Write(const std::string_view str) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const std::string_view str) override final;

    virtual HRESULT Write(bool bBoolean) override final { return m_pWriter->Write(bBoolean); }
    virtual HRESULT WriteNamed(LPCWSTR szName, bool bBoolean) override final
    {
        return m_pWriter->WriteNamed(szName, bBoolean);
    }

    virtual HRESULT Write(ULONG32 dwValue, bool bInHex = false) override final
    {
        return m_pWriter->Write(dwValue, bInHex);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, ULONG32 dwValue, bool bInHex = false) override final
    {
        return m_pWriter->WriteNamed(szName, dwValue, bInHex);
    }

    virtual HRESULT Write(LONG32 iValue, bool bInHex = false) override final
    {
        return m_pWriter->Write(iValue, bInHex);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, LONG32 iValue, bool bInHex = false) override final
    {
        return m_pWriter->WriteNamed(szName, iValue, bInHex);
    }

    virtual HRESULT Write(ULONG64 ullValue, bool bInHex = false) override final
    {
        return m_pWriter->Write(ullValue, bInHex);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, ULONG64 ullValue, bool bInHex = false) override final
    {
        return m_pWriter->WriteNamed(szName, ullValue, bInHex);
    }

    virtual HRESULT Write(LONG64 llValue, bool bInHex = false) override final
    {
        return m_pWriter->Write(llValue, bInHex);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, LONG64 llValue, bool bInHex = false) override final
    {
        return m_pWriter->WriteNamed(szName, llValue, bInHex);
    }

    virtual HRESULT Write(LARGE_INTEGER ullValue, bool bInHex = false) override final
    {
        return m_pWriter->Write(ullValue, bInHex);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false) override final
    {
        return m_pWriter->WriteNamed(szName, ullValue, bInHex);
    }

    virtual HRESULT WriteFileTime(ULONGLONG fileTime) override final { return m_pWriter->WriteFileTime(fileTime); }
    virtual HRESULT WriteNamedFileTime(LPCWSTR szName, ULONGLONG fileTime) override final
    {
        return m_pWriter->WriteNamedFileTime(szName, fileTime);
    }

    virtual HRESULT WriteAttributes(DWORD dwAttibutes) override final
    {
        return m_pWriter->WriteAttributes(dwAttibutes);
    }
    virtual HRESULT WriteNamedAttributes(LPCWSTR szName, DWORD dwAttributes) override final
    {
        return m_pWriter->WriteNamedAttributes(szName, dwAttributes);
    }

    virtual HRESULT Write(FILETIME fileTime) override final { return m_pWriter->Write(fileTime); }
    virtual HRESULT WriteNamed(LPCWSTR szName, FILETIME fileTime) override final
    {
        return m_pWriter->WriteNamed(szName, fileTime);
    }

    virtual HRESULT Write(const WCHAR* szArray, DWORD dwCharCount) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount) override final;

    virtual HRESULT Write(const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) override final
    {
        return m_pWriter->Write(pSHA1, dwLen, b0xPrefix);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) override final
    {
        return m_pWriter->WriteNamed(szName, pSHA1, dwLen, b0xPrefix);
    }

    virtual HRESULT Write(const CBinaryBuffer& Buffer, bool b0xPrefix) override final
    {
        return m_pWriter->Write(Buffer, b0xPrefix);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix) override final
    {
        return m_pWriter->WriteNamed(szName, Buffer, b0xPrefix);
    }

    virtual HRESULT Write(DWORD dwEnum, const WCHAR* EnumValues[]) override final
    {
        return m_pWriter->Write(dwEnum, EnumValues);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[]) override final
    {
        return m_pWriter->WriteNamed(szName, dwEnum, EnumValues);
    }

    virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final
    {
        return m_pWriter->Write(dwFlags, FlagValues, cSeparator);
    }
    virtual HRESULT
    WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final
    {
        return m_pWriter->WriteNamed(szName, dwFlags, FlagValues, cSeparator);
    }

    virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[]) override final
    {
        return m_pWriter->Write(dwFlags, FlagValues);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[]) override final
    {
        return m_pWriter->WriteNamed(szName, dwFlags, FlagValues);
    }

    virtual HRESULT Write(IN_ADDR& ip) override final { return m_pWriter->Write(ip); }
    virtual HRESULT WriteNamed(LPCWSTR szName, IN_ADDR& ip) override final { return m_pWriter->WriteNamed(szName, ip); }

    virtual HRESULT Write(IN6_ADDR& ip) override final { return m_pWriter->Write(ip); }
    virtual HRESULT WriteNamed(LPCWSTR szName, IN6_ADDR& ip) override final
    {
        return m_pWriter->WriteNamed(szName, ip);
    }

    virtual HRESULT Write(const GUID& guid) override final { return m_pWriter->Write(guid); }
    virtual HRESULT WriteNamed(LPCWSTR szName, const GUID& guid) override final
    {
        return m_pWriter->WriteNamed(szName, guid);
    }

    virtual HRESULT WriteComment(LPCWSTR szComment) override final;

    virtual ~RobustStructuredWriter();

protected:
    HRESULT WriteFormated_(const std::wstring_view& szFormat, fmt::wformat_args args) override final;
    HRESULT WriteFormated_(const std::string_view& szFormat, fmt::format_args args) override final;
    HRESULT
    WriteNamedFormated_(LPCWSTR szName, const std::wstring_view& szFormat, fmt::wformat_args args) override final;
    HRESULT
    WriteNamedFormated_(LPCWSTR szName, const std::string_view& szFormat, fmt::format_args args) override final;
};

}  // namespace Orc
#pragma managed(pop)
