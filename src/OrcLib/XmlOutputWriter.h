//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "StructuredOutputWriter.h"

#include "OutputSpec.h"

#include "XmlLiteExtension.h"

#include <stack>

#pragma managed(push, off)

struct IXmlWriter;

namespace Orc {

class ByteStream;

namespace StructuredOutput::XML {

class Writer : public StructuredOutput::Writer
{
protected:
    std::shared_ptr<XmlLiteExtension> m_xmllite;
    CComPtr<IXmlWriter> m_pWriter;
    std::stack<std::wstring> m_collectionStack;

public:
    Writer(std::unique_ptr<Options>&& pOptions);
    Writer(const Writer&) = delete;

    HRESULT SetOutput(std::shared_ptr<ByteStream> stream);

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

    virtual HRESULT Write(bool bBoolean) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, bool bBoolean) override final;

    virtual HRESULT Write(uint32_t dwValue, bool bInHex = false) override final { return Write_(dwValue, bInHex); };
    virtual HRESULT WriteNamed(LPCWSTR szName, uint32_t dwValue, bool bInHex = false) override final
    {
        return WriteNamed_(szName, dwValue, bInHex);
    };

    virtual HRESULT Write(int32_t dwValue, bool bInHex = false) override final { return Write_(dwValue, bInHex); };
    virtual HRESULT WriteNamed(LPCWSTR szName, int32_t dwValue, bool bInHex = false) override final
    {
        return WriteNamed_(szName, dwValue, bInHex);
    };

    virtual HRESULT Write(uint64_t ullValue, bool bInHex = false) override final { return Write_(ullValue, bInHex); };
    virtual HRESULT WriteNamed(LPCWSTR szName, uint64_t ullValue, bool bInHex = false) override final
    {
        return WriteNamed_(szName, ullValue, bInHex);
    }

    virtual HRESULT Write(int64_t ullValue, bool bInHex = false) override final { return Write_(ullValue, bInHex); };
    virtual HRESULT WriteNamed(LPCWSTR szName, int64_t ullValue, bool bInHex = false) override final
    {
        return WriteNamed_(szName, ullValue, bInHex);
    }

    virtual HRESULT Write(LARGE_INTEGER ullValue, bool bInHex = false) override final
    {
        return Write_(ullValue, bInHex);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false) override final
    {
        return WriteNamed_(szName, ullValue, bInHex);
    }

    virtual HRESULT WriteAttributes(DWORD dwAttibutes) override final;
    virtual HRESULT WriteNamedAttributes(LPCWSTR szName, DWORD dwAttibutes) override final;

    virtual HRESULT WriteFileTime(ULONGLONG fileTime) override final;
    virtual HRESULT WriteNamedFileTime(LPCWSTR szName, ULONGLONG fileTime) override final;

    virtual HRESULT Write(FILETIME fileTime) override final { return Write_(fileTime); }
    virtual HRESULT WriteNamed(LPCWSTR szName, FILETIME fileTime) override final
    {
        return WriteNamed_(szName, fileTime);
    }

    virtual HRESULT Write(const WCHAR* szArray, DWORD dwCharCount) override final
    {
        return Write_(szArray, dwCharCount);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount) override final
    {
        return WriteNamed_(szName, szArray, dwCharCount);
    }

    virtual HRESULT Write(const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) override final
    {
        return Write_(pSHA1, dwLen, b0xPrefix);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) override final
    {
        return WriteNamed_(szName, pSHA1, dwLen, b0xPrefix);
    }

    virtual HRESULT Write(const CBinaryBuffer& Buffer, bool b0xPrefix) override final
    {
        return Write_(Buffer, b0xPrefix);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix) override final
    {
        return WriteNamed_(szName, Buffer, b0xPrefix);
    }

    virtual HRESULT Write(DWORD dwEnum, const WCHAR* EnumValues[]) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[]) override final;

    virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final
    {
        return Write_(dwFlags, FlagValues, cSeparator);
    }
    virtual HRESULT
    WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final
    {
        return WriteNamed_(szName, dwFlags, FlagValues, cSeparator);
    }
    virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[]) override final
    {
        return Write_(dwFlags, FlagValues);
    }
    virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[]) override final
    {
        return WriteNamed_(szName, dwFlags, FlagValues);
    }

    virtual HRESULT Write(IN_ADDR& ip) override final { return Write_(ip); }
    virtual HRESULT WriteNamed(LPCWSTR szName, IN_ADDR& ip) override final { return WriteNamed_(szName, ip); }

    virtual HRESULT Write(IN6_ADDR& ip) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, IN6_ADDR& ip) override final;

    virtual HRESULT Write(const GUID& guid) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const GUID& guid) override final;

    virtual HRESULT WriteComment(LPCWSTR szComment) override final;

    ~Writer();

protected:
    virtual HRESULT WriteFormated_(const std::wstring_view& szFormat, wformat_args args) override final;
    virtual HRESULT WriteFormated_(const std::string_view& szFormat, format_args args) override final;
    virtual HRESULT
    WriteNamedFormated_(LPCWSTR szName, const std::wstring_view& szFormat, wformat_args args) override final;
    virtual HRESULT
    WriteNamedFormated_(LPCWSTR szName, const std::string_view& szFormat, format_args args) override final;

private:
    template <typename... Args>
    HRESULT WriteNamed_(LPCWSTR szName, Args&&... args)
    {
        _Buffer buffer;

        if (auto hr = StructuredOutput::Writer::WriteBuffer(buffer, std::forward<Args>(args)...); FAILED(hr))
            return hr;

        if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, buffer.empty() ? L"" : buffer.get());
            FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
        return S_OK;
    }
    template <typename... Args>
    HRESULT Write_(Args&&... args)
    {
        _Buffer buffer;

        if (auto hr = StructuredOutput::Writer::WriteBuffer(buffer, std::forward<Args>(args)...); FAILED(hr))
            return hr;

        if (auto hr = m_pWriter->WriteString(buffer.empty() ? L"" : buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
        return S_OK;
    }
};
}  // namespace StructuredOutput::XML

using XmlOutputWriter = StructuredOutput::XML::Writer;

}  // namespace Orc
#pragma managed(pop)
