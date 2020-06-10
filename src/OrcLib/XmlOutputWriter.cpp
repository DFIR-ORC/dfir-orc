//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "XmlOutputWriter.h"
#include "XmlLiteExtension.h"

#include "OutputSpec.h"

#include "ByteStream.h"

#include <xmllite.h>

using namespace Orc;

using namespace std;
using namespace std::string_view_literals;


Orc::StructuredOutput::XML::Writer::Writer(logger pLog, std::unique_ptr<Orc::StructuredOutput::XML::Options>&& pOptions)
    : StructuredOutput::Writer(std::move(pLog), std::move(pOptions))
    , m_pWriter(nullptr)
{
}

HRESULT Orc::StructuredOutput::XML::Writer::SetOutput(const std::shared_ptr<ByteStream> pStream)
{
    HRESULT hr = E_FAIL;
    CComPtr<IStream> stream;

    if (FAILED(hr = ByteStream::Get_IStream(pStream, &stream)))
        return hr;

    CComPtr<IXmlWriter> pWriter;
    CComPtr<IXmlWriterOutput> pWriterOutput;

    m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);
    if (!m_xmllite)
    {
        log::Error(_L_, E_FAIL, L"Failed to load xmllite extension library\r\n");
        return E_POINTER;
    }

    if (FAILED(hr = m_xmllite->CreateXmlWriter(IID_IXmlWriter, (PVOID*)&pWriter, nullptr)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        log::Error(_L_, hr, L"Failed to instantiate Xml writer\r\n");
        return hr;
    }

    auto options = dynamic_cast<Options*>(m_Options.get());
    auto encoding = options ? options->Encoding
                            : OutputSpec::Encoding::UTF8;

    if (encoding == OutputSpec::Encoding::UTF16)
    {
        if (FAILED(hr = m_xmllite->CreateXmlWriterOutputWithEncodingName(stream, nullptr, L"utf-16", &pWriterOutput)))
        {
            XmlLiteExtension::LogError(_L_, hr);
            log::Error(_L_, hr, L"Failed to instantiate Xml writer (with encoding hint)\r\n");
            return hr;
        }
        if (FAILED(hr = pWriter->SetOutput(pWriterOutput)))
        {
            XmlLiteExtension::LogError(_L_, hr);
            log::Error(_L_, hr, L"Failed to set output stream\r\n", hr);
            return hr;
        }
    }
    else
    {
        if (FAILED(hr = pWriter->SetOutput(stream)))
        {
            XmlLiteExtension::LogError(_L_, hr);
            log::Error(_L_, hr, L"Failed to set output stream\r\n", hr);
            return hr;
        }
    }

    if (FAILED(hr = pWriter->SetProperty(XmlWriterProperty_Indent, TRUE)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        log::Error(_L_, hr, L"Failed to set indentation property\r\n", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->WriteStartDocument(XmlStandalone_Omit)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        log::Error(_L_, hr, L"Failed to write start document\r\n", hr);
        return hr;
    }

    m_pWriter = pWriter;

    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Close()
{
    HRESULT hr = E_FAIL;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (FAILED(hr = m_pWriter->Flush()))
    {
        XmlLiteExtension::LogError(_L_, hr);
        m_pWriter.Release();
        return hr;
    }
    m_pWriter.Release();
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::BeginElement(LPCWSTR szElement)
{
    HRESULT hr = E_FAIL;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (FAILED(hr = m_pWriter->WriteStartElement(NULL, szElement, NULL)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::EndElement(LPCWSTR szElement)
{
    DBG_UNREFERENCED_PARAMETER(szElement);

    HRESULT hr = E_FAIL;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (FAILED(hr = m_pWriter->WriteEndElement()))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::BeginCollection(LPCWSTR szCollection)
{
    return BeginElement(szCollection);
}

HRESULT Orc::StructuredOutput::XML::Writer::EndCollection(LPCWSTR szCollection)
{
    return EndElement(szCollection);
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(LPCWSTR szValue)
{
    HRESULT hr = E_FAIL;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (FAILED(hr = m_pWriter->WriteString(szValue)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, LPCWSTR szValue)
{
    HRESULT hr = E_FAIL;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szValue)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}


HRESULT Orc::StructuredOutput::XML::Writer::WriteFormated(const WCHAR* szFormat, ...)
{
    va_list argList;
    va_start(argList, szFormat);

    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 1024;
    WCHAR szBuffer[dwMaxChar];

    hr = StringCchVPrintfW(szBuffer, dwMaxChar, szFormat, argList);
    va_end(argList);

    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to write formated string\r\n");
        return hr;
    }

    if (FAILED(hr = m_pWriter->WriteString(szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}


HRESULT Orc::StructuredOutput::XML::Writer::WriteNamedFormated(LPCWSTR szName, const WCHAR* szFormat, ...)
{
    va_list argList;
    va_start(argList, szFormat);

    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 1024;
    WCHAR szBuffer[dwMaxChar];

    hr = StringCchVPrintfW(szBuffer, dwMaxChar, szFormat, argList);
    va_end(argList);

    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to write formated string\r\n");
        return hr;
    }

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}
HRESULT Orc::StructuredOutput::XML::Writer::WriteAttributes(DWORD dwFileAttributes)
{
    _Buffer buffer;

    if (auto hr = WriteAttributesBuffer(buffer, dwFileAttributes); FAILED(hr))
        return hr;

    if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamedAttributes(LPCWSTR szName, DWORD dwFileAttributes)
{
    _Buffer buffer;

    if (auto hr = WriteAttributesBuffer(buffer, dwFileAttributes); FAILED(hr))
        return hr;

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, buffer.get()); FAILED(hr))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_pWriter->WriteString(bBoolean ? L"true" : L"false")))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, bBoolean ? L"true" : L"false")))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(DWORD dwEnum, const WCHAR* EnumValues[])
{
    HRESULT hr = E_FAIL;
    unsigned int i = 0;
    const WCHAR* szValue = NULL;

    while (((EnumValues[i]) != NULL) && i <= dwEnum)
    {
        if (i == dwEnum)
        {
            szValue = EnumValues[i];
            break;
        }
        i++;
    }

    if (szValue == NULL)
        szValue = L"IllegalEnumValue";

    if (FAILED(hr = m_pWriter->WriteString(szValue)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[])
{
    HRESULT hr = E_FAIL;
    unsigned int i = 0;

    while (((EnumValues[i]) != NULL) && i <= dwEnum)
    {
        if (i == dwEnum)
        {
            if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, EnumValues[i])))
            {
                XmlLiteExtension::LogError(_L_, hr);
                return hr;
            }
            return S_OK;
        }
        i++;
    }

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, L"IllegalEnumValue", NULL, EnumValues[i])))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}


HRESULT Orc::StructuredOutput::XML::Writer::Write(IN6_ADDR& ip)
{
    return Write(ip.u.Byte, 16, false);
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, IN6_ADDR& ip)
{
    return WriteNamed(szName, ip.u.Byte, 16, false);
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(const GUID& guid)
{
    WCHAR szCLSID[MAX_GUID_STRLEN];
    if (StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
    {
        return Write(szCLSID);
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, const GUID& guid)
{
    WCHAR szCLSID[MAX_GUID_STRLEN];
    if (StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
    {
        return WriteNamed(szName, szCLSID);
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteComment(LPCWSTR szComment)
{
    return m_pWriter->WriteComment(szComment);
}

Orc::StructuredOutput::XML::Writer::~Writer() {}
