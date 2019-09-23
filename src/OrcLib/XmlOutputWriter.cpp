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

HRESULT XmlOutputWriter::SetOutput(const std::shared_ptr<ByteStream> pStream, OutputSpec::Encoding Encoding)
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

    if (Encoding == OutputSpec::Encoding::UTF16)
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

HRESULT XmlOutputWriter::Close()
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

HRESULT XmlOutputWriter::BeginElement(LPCWSTR szElement)
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

HRESULT XmlOutputWriter::EndElement(LPCWSTR szElement)
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

HRESULT XmlOutputWriter::BeginCollection(LPCWSTR szCollection)
{
    return BeginElement(szCollection);
}

HRESULT XmlOutputWriter::EndCollection(LPCWSTR szCollection)
{
    return EndElement(szCollection);
}

HRESULT XmlOutputWriter::WriteNameValuePair(LPCWSTR szName, LPCWSTR szValue)
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

HRESULT XmlOutputWriter::WriteNameFormatedStringPair(LPCWSTR szName, const WCHAR* szFormat, ...)
{
    HRESULT hr = E_FAIL;

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

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameValuePair(LPCWSTR szName, DWORD dwValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 24;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(hr = StringCchPrintfW(szBuffer, dwMaxChar, bInHex ? L"0x%.8X" : L"%d", dwValue)))
    {
        log::Error(_L_, hr, L"Failed to write integer\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameValuePair(LPCWSTR szName, ULONGLONG ullValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 24;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(hr = StringCchPrintfW(szBuffer, dwMaxChar, bInHex ? L"0x%.16I64X" : L"%I64d", ullValue)))
    {
        log::Error(_L_, hr, L"Failed to write integer\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameValuePair(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex)
{
    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 24;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(hr = StringCchPrintfW(szBuffer, dwMaxChar, bInHex ? L"0x%.16I64X" : L"%I64d", ullValue.QuadPart)))
    {
        log::Error(_L_, hr, L"Failed to write integer\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameSizePair(LPCWSTR szName, size_t size, bool bInHex)
{
    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 24;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(hr = StringCchPrintfW(szBuffer, dwMaxChar, bInHex ? L"0x%IX" : L"%Iu", size)))
    {
        log::Error(_L_, hr, L"Failed to write integer\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameAttributesPair(LPCWSTR szName, DWORD dwFileAttributes)
{
    HRESULT hr = E_FAIL;
    const DWORD dwMaxChar = 14;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(
            hr = StringCchPrintfW(
                szBuffer,
                dwMaxChar,
                L"%c%c%c%c%c%c%c%c%c%c%c%c%c",
                dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ? L'A' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ? L'C' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? L'D' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ? L'E' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? L'H' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_NORMAL ? L'N' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_OFFLINE ? L'O' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_READONLY ? L'R' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? L'?' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? L'?' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? L'S' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY ? L'T' : L'.',
                dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL ? L'V' : L'.')))
    {
        log::Error(_L_, hr, L"Failed to write Attributes\r\n");
        return hr;
    }

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameFileTimePair(LPCWSTR szName, FILETIME fileTime)
{
    HRESULT hr = E_FAIL;

    // Convert the Create time to System time.
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&fileTime, &stUTC);

    const DWORD dwMaxChar = 64;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(
            hr = StringCchPrintfW(
                szBuffer,
                dwMaxChar,
                L"%u-%02u-%02u %02u:%02u:%02u.%03u",
                stUTC.wYear,
                stUTC.wMonth,
                stUTC.wDay,
                stUTC.wHour,
                stUTC.wMinute,
                stUTC.wSecond,
                stUTC.wMilliseconds)))
    {
        log::Error(_L_, hr, L"Failed to write File time\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameFileTimePair(LPCWSTR szName, LONGLONG fileTime)
{
    HRESULT hr = E_FAIL;

    // Convert the Create time to System time.
    SYSTEMTIME stUTC;
    FileTimeToSystemTime((FILETIME*)(&fileTime), &stUTC);

    const DWORD dwMaxChar = 64;
    WCHAR szBuffer[dwMaxChar];

    if (FAILED(
            hr = StringCchPrintfW(
                szBuffer,
                dwMaxChar,
                L"%u-%02u-%02u %02u:%02u:%02u.%03u",
                stUTC.wYear,
                stUTC.wMonth,
                stUTC.wDay,
                stUTC.wHour,
                stUTC.wMinute,
                stUTC.wSecond,
                stUTC.wMilliseconds)))
    {
        log::Error(_L_, hr, L"Failed to write File time\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szBuffer)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameCharArrayPair(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;

    buffer.SetCount(dwCharCount * sizeof(WCHAR));

    if (FAILED(hr = StringCchPrintfW((LPWSTR)buffer.GetData(), dwCharCount, L"%.*s", dwCharCount, szArray)))
    {
        log::Error(_L_, hr, L"Failed to write WChar Array\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, (LPWSTR)buffer.GetData())))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameCharArrayPair(LPCWSTR szName, const CHAR* szArray, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;

    buffer.SetCount(msl::utilities::SafeInt<USHORT>(dwCharCount) * sizeof(WCHAR));

    if (FAILED(hr = StringCchPrintfW((LPWSTR)buffer.GetData(), dwCharCount, L"%.*S", dwCharCount, szArray)))
    {
        log::Error(_L_, hr, L"Failed to write WChar Array\r\n");
        return hr;
    }
    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, (LPWSTR)buffer.GetData())))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameBytesInHexPair(LPCWSTR szName, const BYTE pBytes[], DWORD dwLen, bool b0xPrefix)
{
    using namespace msl::utilities;

    HRESULT hr = E_FAIL;

    if (dwLen == 0)
    {
        if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, L"")))
        {
            XmlLiteExtension::LogError(_L_, hr);
            return hr;
        }
        return S_OK;
    }

    const DWORD cchLen = (SafeInt<DWORD>(dwLen) * 2) + ((b0xPrefix ? SafeInt<DWORD>(2) : SafeInt<DWORD>(0)) + 1);

    DWORD buffer_size = 0;

    if (!msl::utilities::SafeMultiply(cchLen, sizeof(WCHAR), buffer_size))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    CBinaryBuffer buffer;
    buffer.SetCount(buffer_size);

    LPWSTR pCur = NULL;
    size_t cchLeft = 0L;
    if (b0xPrefix)
    {
        StringCchCopyExW((LPWSTR)buffer.GetData(), cchLen, L"0x", &pCur, &cchLeft, 0L);
    }
    else
    {
        pCur = (LPWSTR)buffer.GetData();
        cchLeft = cchLen;
    }

    for (DWORD i = 0; i < dwLen; i++)
    {
        if (FAILED(hr = StringCchPrintfExW(pCur, cchLeft, &pCur, &cchLeft, 0L, L"%2.2X", pBytes[i])))
        {
            log::Error(_L_, hr, L"Failed to write BYTE Array\r\n");
            return hr;
        }
    }

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, (LPWSTR)buffer.GetData())))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameBytesInHexPair(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix)
{
    return WriteNameBytesInHexPair(szName, Buffer.GetData(), (DWORD)Buffer.GetCount(), b0xPrefix);
}

HRESULT XmlOutputWriter::WriteNameBoolPair(LPCWSTR szName, bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, bBoolean ? L"true" : L"false")))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameEnumPair(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[])
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

    if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szValue)))
    {
        XmlLiteExtension::LogError(_L_, hr);
        return hr;
    }
    return S_OK;
}

HRESULT
XmlOutputWriter::WriteNameFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
{
    HRESULT hr = E_FAIL;
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
        if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, (WCHAR*)buffer.GetData())))
        {
            XmlLiteExtension::LogError(_L_, hr);
            return hr;
        }
    }
    else
    {
        // No flags where recognised, write value in Hex
        if (FAILED(WriteNameValuePair(szName, dwFlags, true)))
            return hr;
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameExactFlagsPair(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    HRESULT hr = E_FAIL;
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
        if (FAILED(WriteNameValuePair(szName, dwFlags, true)))
            return hr;
    }
    else
    {
        if (FAILED(hr = m_pWriter->WriteAttributeString(NULL, szName, NULL, szValue)))
        {
            XmlLiteExtension::LogError(_L_, hr);
            return hr;
        }
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteNameIPPair(LPCWSTR szName, IN_ADDR& ip)
{
    return WriteNameFormatedStringPair(
        szName, L"%d.%d.%d.%d", ip.S_un.S_un_b.s_b1, ip.S_un.S_un_b.s_b2, ip.S_un.S_un_b.s_b3, ip.S_un.S_un_b.s_b4);
}

HRESULT XmlOutputWriter::WriteNameIPPair(LPCWSTR szName, IN6_ADDR& ip)
{
    return WriteNameBytesInHexPair(szName, ip.u.Byte, 16, false);
}

HRESULT XmlOutputWriter::WriteNameGUIDPair(LPCWSTR szName, const GUID& guid)
{
    WCHAR szCLSID[MAX_GUID_STRLEN];
    if (StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
    {
        return WriteNameValuePair(szName, szCLSID);
    }
    return S_OK;
}

HRESULT XmlOutputWriter::WriteComment(LPCWSTR szComment)
{
    return m_pWriter->WriteComment(szComment);
}

HRESULT XmlOutputWriter::WriteString(LPCWSTR szString)
{
    return m_pWriter->WriteString(szString);
    ;
}

XmlOutputWriter::~XmlOutputWriter() {}
