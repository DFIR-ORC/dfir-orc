//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "RobustStructuredWriter.h"

#include "TableOutputWriter.h"

#include "Unicode.h"

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

HRESULT RobustStructuredWriter::WriteFormated(const WCHAR* szFormat, ...)
{
    HRESULT hr = E_FAIL;
    std::wstring strValue;

    va_list argList;
    va_start(argList, szFormat);

    const DWORD dwMaxChar = 1024;
    WCHAR szBuffer[dwMaxChar];
    hr = StringCchVPrintfW(szBuffer, dwMaxChar, szFormat, argList);
    va_end(argList);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to write formated string\r\n");
        return hr;
    }

    if (FAILED(hr = SanitizeString(xml_attr_value_table, szBuffer, strValue)))
        return hr;

    return m_pWriter->Write(strValue.c_str());
}

HRESULT RobustStructuredWriter::WriteNamedFormated(LPCWSTR szName, const WCHAR* szFormat, ...)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, strValue;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    va_list argList;
    va_start(argList, szFormat);

    const DWORD dwMaxChar = 1024;
    WCHAR szBuffer[dwMaxChar];
    hr = StringCchVPrintfW(szBuffer, dwMaxChar, szFormat, argList);
    va_end(argList);
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to write formated string\r\n");
        return hr;
    }

    if (FAILED(hr = SanitizeString(xml_attr_value_table, szBuffer, strValue)))
        return hr;

    return m_pWriter->WriteNamed(strName.c_str(), strValue.c_str());
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

HRESULT RobustStructuredWriter::Write(const WCHAR* szArray, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;
    buffer.SetCount(msl::utilities::SafeInt<DWORD>(dwCharCount) * sizeof(WCHAR));

    if (FAILED(hr = StringCchPrintfW((LPWSTR)buffer.GetData(), dwCharCount, L"%.*s", dwCharCount, szArray)))
    {
        log::Error(_L_, hr, L"Failed to write WChar Array\r\n");
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
        log::Error(_L_, hr, L"Failed to write WChar Array\r\n");
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
