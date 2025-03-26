//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "RobustStructuredWriter.h"

#include "TableOutputWriter.h"
#include "BinaryBuffer.h"
#include "Unicode.h"
#include "WideAnsi.h"

using namespace std;

using namespace Orc;

HRESULT RobustStructuredWriter::BeginElement(LPCWSTR szElement)
{
    HRESULT hr = E_FAIL;
    std::wstring strElt;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szElement, strElt)))
        return hr;

    return m_pWriter->BeginElement(strElt.c_str());
}

HRESULT RobustStructuredWriter::EndElement(LPCWSTR szElement)
{
    HRESULT hr = E_FAIL;
    std::wstring strElt;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szElement, strElt)))
        return hr;

    return m_pWriter->EndElement(strElt.c_str());
}

HRESULT RobustStructuredWriter::BeginCollection(LPCWSTR szCollection)
{
    HRESULT hr = E_FAIL;
    std::wstring strElt;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szCollection, strElt)))
        return hr;

    return m_pWriter->BeginCollection(strElt.c_str());
}

HRESULT RobustStructuredWriter::EndCollection(LPCWSTR szCollection)
{
    HRESULT hr = E_FAIL;
    std::wstring strElt;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szCollection, strElt)))
        return hr;

    return m_pWriter->EndCollection(strElt.c_str());
}

HRESULT RobustStructuredWriter::WriteFormated_(std::wstring_view szFormat, fmt::wformat_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<WCHAR, ORC_MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::wstring_view result_string = buffer.size() > 0 ? std::wstring_view(buffer.get(), buffer.size()) : L""sv;

    return Write(result_string);
}

HRESULT RobustStructuredWriter::WriteFormated_(std::string_view szFormat, fmt::format_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<CHAR, ORC_MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::string_view result_string = buffer.size() > 0 ? std::string_view(buffer.get(), buffer.size()) : ""sv;

    return Write(result_string);
}

HRESULT
RobustStructuredWriter::WriteNamedFormated_(LPCWSTR szName, std::wstring_view szFormat, fmt::wformat_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<WCHAR, ORC_MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::wstring_view result_string = buffer.size() > 0 ? std::wstring_view(buffer.get(), buffer.size()) : L""sv;

    return WriteNamed(szName, result_string);
}

HRESULT
RobustStructuredWriter::WriteNamedFormated_(LPCWSTR szName, std::string_view szFormat, fmt::format_args args)
{
    using namespace std::string_view_literals;

    if (m_pWriter == nullptr)
        return E_POINTER;

    Buffer<CHAR, ORC_MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::string_view result_string = buffer.size() > 0 ? std::string_view(buffer.get(), buffer.size()) : ""sv;

    return WriteNamed(szName, result_string);
}

HRESULT RobustStructuredWriter::Write(LPCWSTR szValue)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, strValue;

    if (FAILED(hr = SanitizeString(xml_attr_value_table, szValue, strValue)))
        return hr;

    return m_pWriter->Write(strValue.c_str());
}

HRESULT RobustStructuredWriter::WriteNamed(LPCWSTR szName, LPCWSTR szValue)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, strValue;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, szValue, strValue)))
        return hr;

    return m_pWriter->WriteNamed(strName.c_str(), strValue.c_str());
}

HRESULT RobustStructuredWriter::Write(const std::wstring_view str)
{
    HRESULT hr = E_FAIL;
    std::wstring strValue;

    if (FAILED(hr = SanitizeString(xml_attr_value_table, str, strValue)))
        return hr;

    return m_pWriter->Write(strValue);
}

HRESULT RobustStructuredWriter::WriteNamed(LPCWSTR szName, const std::wstring_view str)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, strValue;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, str, strValue)))
        return hr;

    return m_pWriter->WriteNamed(strName.c_str(), strValue.c_str());
}

HRESULT RobustStructuredWriter::Write(const std::wstring& str)
{
    HRESULT hr = E_FAIL;
    std::wstring strValue;

    if (FAILED(hr = SanitizeString(xml_attr_value_table, str, strValue)))
        return hr;

    return m_pWriter->Write(strValue);
}

HRESULT RobustStructuredWriter::WriteNamed(LPCWSTR szName, const std::wstring& str)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, strValue;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, str, strValue)))
        return hr;

    return m_pWriter->WriteNamed(strName.c_str(), strValue.c_str());
}

HRESULT RobustStructuredWriter::Write(std::string_view str)
{
    HRESULT hr = E_FAIL;
    std::wstring wstr, strValue;

    if (FAILED(hr = AnsiToWide(str, wstr)))
        return hr;

    if (FAILED(hr = SanitizeString(xml_attr_value_table, wstr, strValue)))
        return hr;

    return m_pWriter->Write(strValue);
}

HRESULT RobustStructuredWriter::WriteNamed(LPCWSTR szName, std::string_view str)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, wstr, strValue;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    if (FAILED(hr = AnsiToWide(str, wstr)))
        return hr;

    if (FAILED(hr = SanitizeString(xml_attr_value_table, wstr, strValue)))
        return hr;

    return m_pWriter->WriteNamed(strName.c_str(), strValue);
}

HRESULT RobustStructuredWriter::Write(const WCHAR* szArray, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;
    buffer.SetCount(msl::utilities::SafeInt<DWORD>(dwCharCount) * sizeof(WCHAR));

    if (FAILED(hr = StringCchPrintfW((LPWSTR)buffer.GetData(), dwCharCount, L"%.*s", dwCharCount, szArray)))
    {
        Log::Error("Failed to write WChar Array [{}]", SystemError(hr));
        return hr;
    }

    wstring strValue;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, buffer.GetP<WCHAR>(), strValue)))
        return hr;

    if (FAILED(hr = m_pWriter->Write(strValue.c_str())))
    {
        return hr;
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNamed(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    CBinaryBuffer buffer;
    buffer.SetCount(msl::utilities::SafeInt<DWORD>(dwCharCount) * sizeof(WCHAR));

    if (FAILED(hr = StringCchPrintfW((LPWSTR)buffer.GetData(), dwCharCount, L"%.*s", dwCharCount, szArray)))
    {
        Log::Error("Failed to write WChar Array [{}]", SystemError(hr));
        return hr;
    }

    wstring strValue;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, buffer.GetP<WCHAR>(), strValue)))
        return hr;

    if (FAILED(hr = m_pWriter->WriteNamed(strName.c_str(), strValue.c_str())))
    {
        return hr;
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteComment(LPCWSTR szComment)
{
    HRESULT hr = E_FAIL;

    std::wstring strComment;
    if (FAILED(hr = SanitizeString(xml_comment_table, szComment, strComment)))
        return hr;
    return m_pWriter->WriteComment(strComment.c_str());
}

HRESULT RobustStructuredWriter::Close()
{
    return m_pWriter->Close();
}

RobustStructuredWriter::~RobustStructuredWriter() {}
