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

HRESULT RobustStructuredWriter::WriteNameValuePair(LPCWSTR szName, LPCWSTR szValue)
{
    HRESULT hr = E_FAIL;
    std::wstring strName, strValue;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, szValue, strValue)))
        return hr;

    return m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str());
}

HRESULT RobustStructuredWriter::WriteNameFormatedStringPair(LPCWSTR szName, const WCHAR* szFormat, ...)
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

    return m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str());
}

HRESULT RobustStructuredWriter::WriteNameValuePair(LPCWSTR szName, DWORD dwValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    return m_pWriter->WriteNameValuePair(strName.c_str(), dwValue, bInHex);
}

HRESULT RobustStructuredWriter::WriteNameValuePair(LPCWSTR szName, ULONGLONG ullValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    return m_pWriter->WriteNameValuePair(strName.c_str(), ullValue, bInHex);
}

HRESULT RobustStructuredWriter::WriteNameValuePair(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    return m_pWriter->WriteNameValuePair(strName.c_str(), ullValue, bInHex);
}

HRESULT RobustStructuredWriter::WriteNameSizePair(LPCWSTR szName, size_t ullValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    return m_pWriter->WriteNameSizePair(strName.c_str(), ullValue, bInHex);
}

HRESULT RobustStructuredWriter::WriteNameAttributesPair(LPCWSTR szName, DWORD dwFileAttibutes)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    return m_pWriter->WriteNameAttributesPair(strName.c_str(), dwFileAttibutes);
}

HRESULT RobustStructuredWriter::WriteNameFileTimePair(LPCWSTR szName, FILETIME fileTime)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;
    return m_pWriter->WriteNameFileTimePair(strName.c_str(), fileTime);
}

HRESULT RobustStructuredWriter::WriteNameFileTimePair(LPCWSTR szName, LONGLONG fileTime)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;

    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    if (FAILED(hr = m_pWriter->WriteNameFileTimePair(strName.c_str(), fileTime)))
    {
        return hr;
    }

    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNameCharArrayPair(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount)
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

    if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str())))
    {
        return hr;
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNameCharArrayPair(LPCWSTR szName, const CHAR* szArray, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    CBinaryBuffer buffer;
    buffer.SetCount((msl::utilities::SafeInt<DWORD>(dwCharCount) + 1) * sizeof(WCHAR));

    if (FAILED(hr = StringCchPrintfW((LPWSTR)buffer.GetData(), dwCharCount, L"%.*s", dwCharCount + 1, szArray)))
    {
        log::Error(_L_, hr, L"Failed to write WChar Array\r\n");
        return hr;
    }

    wstring strValue;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, buffer.GetP<WCHAR>(), strValue)))
        return hr;

    if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str())))
    {
        return hr;
    }
    return S_OK;
}

HRESULT
RobustStructuredWriter::WriteNameBytesInHexPair(LPCWSTR szName, const BYTE pBytes[], DWORD dwLen, bool b0xPrefix)
{
    using namespace msl::utilities;

    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;

    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    if (dwLen == 0)
    {
        if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), L"")))
        {
            return hr;
        }
        return S_OK;
    }

    const DWORD cchLen = (SafeInt<DWORD>(dwLen) * 2) + ((b0xPrefix ? 2 : 0) + 1);
    buffer.SetCount(SafeInt<DWORD>(cchLen) * sizeof(WCHAR));

    LPWSTR pCur = NULL;
    size_t cchLeft = 0L;
    if (b0xPrefix)
    {
        if (FAILED(hr = StringCchCopyExW((LPWSTR)buffer.GetData(), cchLen, L"0x", &pCur, &cchLeft, 0L)))
            return hr;
        pCur += 2;
        cchLeft -= 2;
    }
    else
    {
        pCur = (LPWSTR)buffer.GetData();
        cchLeft = cchLen;
    }

    if (pCur != NULL)
    {
        for (DWORD i = 0; i < dwLen; i++)
        {
            if (FAILED(hr = StringCchPrintfExW(pCur, cchLeft, &pCur, &cchLeft, 0L, L"%2.2X", pBytes[i])))
            {
                log::Error(_L_, hr, L"Failed to write BYTE Array\r\n");
                return hr;
            }
        }

        if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), buffer.GetP<WCHAR>())))
        {
            return hr;
        }
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNameBytesInHexPair(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix)
{
    return RobustStructuredWriter::WriteNameBytesInHexPair(
        szName, Buffer.GetData(), (DWORD)Buffer.GetCount(), b0xPrefix);
}

HRESULT RobustStructuredWriter::WriteNameBoolPair(LPCWSTR szName, bool bBoolean)
{
    HRESULT hr = E_FAIL;
    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), bBoolean ? L"true" : L"false")))
    {
        return hr;
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNameEnumPair(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[])
{
    HRESULT hr = E_FAIL;

    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

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

    std::wstring strValue;
    if (FAILED(hr = SanitizeString(xml_attr_value_table, szValue, strValue)))
        return hr;

    if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str())))
    {
        return hr;
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNameFlagsPair(
    LPCWSTR szName,
    DWORD dwFlags,
    const FlagsDefinition FlagValues[],
    WCHAR cSeparator)
{
    HRESULT hr = E_FAIL;

    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    bool bFirst = true;
    int idx = 0;

    size_t cchLen = 1LL;  // start at 1 for trailing \0
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags & FlagValues[idx].dwFlag)
        {
            if (bFirst)
            {
                bFirst = false;
                cchLen += wcslen(FlagValues[idx].szShortDescr);
            }
            else
            {
                cchLen += wcslen(FlagValues[idx].szShortDescr) + 1;
            }
        }
        idx++;
    }

    if (cchLen > 1LL)
    {
        CBinaryBuffer buffer;
        buffer.SetCount(msl::utilities::SafeInt<USHORT>(cchLen) * sizeof(WCHAR));

        bFirst = true;
        idx = 0;
        WCHAR* pCur = (WCHAR*)buffer.GetData();
        size_t cchLeft = cchLen;

        while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
        {
            if (dwFlags & FlagValues[idx].dwFlag)
            {
                if (bFirst)
                {
                    bFirst = false;
                    if (FAILED(
                            hr = StringCchPrintfExW(
                                pCur, cchLeft, &pCur, &cchLeft, 0L, L"%s", FlagValues[idx].szShortDescr)))
                    {
                        log::Error(_L_, hr, L"Failed to write enum value\r\n");
                        return hr;
                    }
                }
                else
                {
                    if (FAILED(
                            hr = StringCchPrintfExW(
                                pCur, cchLeft, &pCur, &cchLeft, 0L, L"%c%s", cSeparator, FlagValues[idx].szShortDescr)))
                    {
                        log::Error(_L_, hr, L"Failed to write enum value\r\n");
                        return hr;
                    }
                }
            }
            idx++;
        }

        std::wstring strValue;
        if (FAILED(hr = SanitizeString(xml_attr_value_table, buffer.GetP<WCHAR>(), strValue)))
            return hr;

        if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str())))
        {
            return hr;
        }
    }
    else
    {
        // No flags where recognised, write value in Hex
        if (FAILED(WriteNameValuePair(strName.c_str(), dwFlags, true)))
            return hr;
    }
    return S_OK;
}

HRESULT
RobustStructuredWriter::WriteNameExactFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    HRESULT hr = E_FAIL;

    std::wstring strName;
    if (FAILED(hr = ReplaceInvalidChars(xml_element_table, szName, strName)))
        return hr;

    unsigned int idx = 0;
    const WCHAR* szValue = NULL;

    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags == FlagValues[idx].dwFlag)
        {
            szValue = FlagValues[idx].szShortDescr;
            break;
        }
        idx++;
    }

    if (szValue == NULL)
    {
        // No flags where recognised, write value in Hex
        if (FAILED(WriteNameValuePair(strName.c_str(), dwFlags, true)))
            return hr;
    }
    else
    {
        std::wstring strValue;
        if (FAILED(hr = SanitizeString(xml_attr_value_table, szValue, strValue)))
            return hr;

        if (FAILED(hr = m_pWriter->WriteNameValuePair(strName.c_str(), strValue.c_str())))
        {
            return hr;
        }
    }
    return S_OK;
}

HRESULT RobustStructuredWriter::WriteNameIPPair(LPCWSTR szName, IN_ADDR& ip)
{
    return RobustStructuredWriter::WriteNameFormatedStringPair(
        szName, L"%d.%d.%d.%d", ip.S_un.S_un_b.s_b1, ip.S_un.S_un_b.s_b2, ip.S_un.S_un_b.s_b3, ip.S_un.S_un_b.s_b4);
}

HRESULT RobustStructuredWriter::WriteNameIPPair(LPCWSTR szName, IN6_ADDR& ip)
{
    return RobustStructuredWriter::WriteNameBytesInHexPair(szName, ip.u.Byte, 16, false);
}

HRESULT RobustStructuredWriter::WriteNameGUIDPair(LPCWSTR szName, const GUID& guid)
{
    WCHAR szCLSID[MAX_GUID_STRLEN];
    if (StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
    {
        return RobustStructuredWriter::WriteNameValuePair(szName, szCLSID);
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

HRESULT RobustStructuredWriter::WriteString(LPCWSTR szString)
{
    HRESULT hr = E_FAIL;

    std::wstring strString;
    if (FAILED(hr = SanitizeString(xml_string_table, szString, strString)))
        return hr;

    return m_pWriter->WriteString(strString.c_str());
    ;
}

HRESULT RobustStructuredWriter::Close()
{
    return m_pWriter->Close();
}

RobustStructuredWriter::~RobustStructuredWriter() {}
