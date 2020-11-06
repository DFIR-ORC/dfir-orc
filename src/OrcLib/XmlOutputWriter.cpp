//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "XmlOutputWriter.h"

#include <xmllite.h>

#include "XmlLiteExtension.h"
#include "OutputSpec.h"
#include "ByteStream.h"
#include "WideAnsi.h"

#include <xmllite.h>
#include <boost/scope_exit.hpp>

using namespace std::string_view_literals;
using namespace Orc;

Orc::StructuredOutput::XML::Writer::Writer(std::unique_ptr<Orc::StructuredOutput::XML::Options>&& pOptions)
    : StructuredOutput::Writer(std::move(pOptions))
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

    m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();
    if (!m_xmllite)
    {
        Log::Error(L"Failed to load xmllite extension library");
        return E_POINTER;
    }

    if (FAILED(hr = m_xmllite->CreateXmlWriter(IID_IXmlWriter, (PVOID*)&pWriter, nullptr)))
    {
        XmlLiteExtension::LogError(hr);
        Log::Error(L"Failed to instantiate Xml writer [{}]", SystemError(hr));
        return hr;
    }

    auto options = dynamic_cast<Options*>(m_Options.get());
    auto encoding = options ? options->Encoding : OutputSpec::Encoding::UTF8;

    if (encoding == OutputSpec::Encoding::UTF16)
    {
        if (FAILED(hr = m_xmllite->CreateXmlWriterOutputWithEncodingName(stream, nullptr, L"utf-16", &pWriterOutput)))
        {
            XmlLiteExtension::LogError(hr);
            Log::Error(L"Failed to instantiate Xml writer with encoding hint [{}]", SystemError(hr));
            return hr;
        }
        if (FAILED(hr = pWriter->SetOutput(pWriterOutput)))
        {
            XmlLiteExtension::LogError(hr);
            Log::Error(L"Failed to set output stream [{}]", SystemError(hr));
            return hr;
        }
    }
    else
    {
        if (FAILED(hr = pWriter->SetOutput(stream)))
        {
            XmlLiteExtension::LogError(hr);
            Log::Error(L"Failed to set output stream [{}]", SystemError(hr));
            return hr;
        }
    }

    if (FAILED(hr = pWriter->SetProperty(XmlWriterProperty_Indent, TRUE)))
    {
        XmlLiteExtension::LogError(hr);
        Log::Error(L"Failed to set indentation property [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->WriteStartDocument(XmlStandalone_Omit)))
    {
        XmlLiteExtension::LogError(hr);
        Log::Error(L"Failed to write start document [{}]", SystemError(hr));
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
        XmlLiteExtension::LogError(hr);
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

    if (szElement)
    {
        if (FAILED(hr = m_pWriter->WriteStartElement(NULL, szElement, NULL)))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else if (!m_collectionStack.empty())
    {
        // if no element is provided, we use the current collection element name
        if (FAILED(hr = m_pWriter->WriteStartElement(NULL, m_collectionStack.top().c_str(), NULL)))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        Log::Error(L"Attempt to add a null element without a current collection");
        return E_INVALIDARG;
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
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::BeginCollection(LPCWSTR szCollection)
{
    m_collectionStack.emplace(szCollection);
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::EndCollection(LPCWSTR szCollection)
{
    if (m_collectionStack.empty())
    {
        Log::Error(L"Attempt to end collection '{}' without a current collection", szCollection);
        return E_INVALIDARG;
    }
    if (m_collectionStack.top().compare(szCollection))
    {
        Log::Error(L"Attempt to end collection '{}' with invalid end ({})", m_collectionStack.top(), szCollection);
        return E_INVALIDARG;
    }
    m_collectionStack.pop();
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(LPCWSTR szValue)
{
    HRESULT hr = E_FAIL;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (m_collectionStack.empty())
    {
        if (FAILED(hr = m_pWriter->WriteString(szValue)))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;

        if (FAILED(hr = m_pWriter->WriteString(szValue)))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
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
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteFormated_(const std::wstring_view& szFormat, fmt::wformat_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<WCHAR, MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
    buffer.append(L"\0");

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;

        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteFormated_(const std::string_view& szFormat, fmt::format_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<CHAR, MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
    buffer.append("\0");

    std::string_view result_string = buffer.size() > 0 ? std::string_view(buffer.get(), buffer.size()) : ""sv;

    auto [hr, wstr] = Orc::AnsiToWide(result_string);
    if (FAILED(hr))
        return hr;

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(wstr.c_str()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;

        if (auto hr = m_pWriter->WriteString(wstr.c_str()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamedFormated_(
    LPCWSTR szName,
    const std::wstring_view& szFormat,
    fmt::wformat_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<WCHAR, MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
    buffer.append(L"\0");

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, buffer.get()); FAILED(hr))
    {
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamedFormated_(
    LPCWSTR szName,
    const std::string_view& szFormat,
    fmt::format_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<CHAR, MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::string_view result_string = buffer.size() > 0 ? std::string_view(buffer.get(), buffer.size()) : ""sv;

    auto [hr, wstr] = Orc::AnsiToWide(result_string);
    if (FAILED(hr))
        return hr;

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, wstr.c_str()); FAILED(hr))
    {
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(const std::wstring_view str)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<WCHAR, MAX_PATH> buffer;
    buffer.set(str.data(), str.size() + 1, str.size());
    buffer.append(L'\0');

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;

        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(const std::wstring& str)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(str.c_str()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;

        if (auto hr = m_pWriter->WriteString(str.c_str()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, const std::wstring_view str)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<WCHAR, MAX_PATH> buffer;
    buffer.set(str.data(), str.size() + 1, str.size());
    buffer.append(L'\0');

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, buffer.get()); FAILED(hr))
    {
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, const std::wstring& str)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, str.c_str()); FAILED(hr))
    {
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(const std::string_view str)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    auto [hr, wstr] = Orc::AnsiToWide(str);
    if (FAILED(hr))
        return hr;

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(wstr.c_str()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;

        if (auto hr = m_pWriter->WriteString(wstr.c_str()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, const std::string_view str)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    auto [hr, wstr] = Orc::AnsiToWide(str);
    if (FAILED(hr))
        return hr;

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, wstr.c_str()); FAILED(hr))
    {
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteAttributes(DWORD dwFileAttributes)
{
    _Buffer buffer;

    if (auto hr = WriteAttributesBuffer(buffer, dwFileAttributes); FAILED(hr))
        return hr;

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;
        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteFileTime(ULONGLONG fileTime)
{
    _Buffer buffer;

    if (auto hr = WriteFileTimeBuffer(buffer, fileTime); FAILED(hr))
        return hr;

    if (m_collectionStack.empty())
    {
        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;
        if (auto hr = m_pWriter->WriteString(buffer.get()); FAILED(hr))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamedFileTime(LPCWSTR szName, ULONGLONG fileTime)
{
    _Buffer buffer;

    if (auto hr = WriteFileTimeBuffer(buffer, fileTime); FAILED(hr))
        return hr;

    if (auto hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, buffer.get()); FAILED(hr))
    {
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
        XmlLiteExtension::LogError(hr);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::Write(bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (m_collectionStack.empty())
    {
        if (FAILED(hr = m_pWriter->WriteString(bBoolean ? L"true" : L"false")))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;
        if (FAILED(hr = m_pWriter->WriteString(bBoolean ? L"true" : L"false")))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::StructuredOutput::XML::Writer::WriteNamed(LPCWSTR szName, bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, bBoolean ? L"true" : L"false")))
    {
        XmlLiteExtension::LogError(hr);
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

    if (m_collectionStack.empty())
    {
        if (FAILED(hr = m_pWriter->WriteString(szValue)))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
    }
    else
    {
        BeginElement(m_collectionStack.top().c_str());
        BOOST_SCOPE_EXIT(this_) { this_->EndElement(this_->m_collectionStack.top().c_str()); }
        BOOST_SCOPE_EXIT_END;
        if (FAILED(hr = m_pWriter->WriteString(szValue)))
        {
            XmlLiteExtension::LogError(hr);
            return hr;
        }
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
                XmlLiteExtension::LogError(hr);
                return hr;
            }
            return S_OK;
        }
        i++;
    }

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, L"IllegalEnumValue", NULL, EnumValues[i])))
    {
        XmlLiteExtension::LogError(hr);
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
