//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "TableOutputWriter.h"
#include "BoundTableRecord.h"

#include "WideAnsi.h"

using namespace Orc;
using namespace Orc::TableOutput;

HRESULT BoundColumn::ClearBoundData()
{
    switch (Type)
    {
        case ColumnType::Nothing:
            break;
        case ColumnType::UInt64Type:
            boundData.LargeInt.QuadPart = 0;
            break;
        case ColumnType::BoolType:
            boundData.Boolean = false;
            break;
        case ColumnType::UInt32Type:
            boundData.Dword = 0L;
            break;
        case ColumnType::TimeStampType:
            ZeroMemory(&(boundData.TimeStamp), sizeof(boundData.TimeStamp));
            boundData.TimeStamp.year = 1;
            boundData.TimeStamp.month = 1;
            boundData.TimeStamp.day = 1;
            break;
        case ColumnType::UTF16Type:
        case ColumnType::XMLType:
            if (dwMaxLen.has_value())
                ZeroMemory(boundData.WString, sizeof(size_t) + dwMaxLen.value() * sizeof(WCHAR));
            else if (dwLen.has_value())
                ZeroMemory(boundData.WString, sizeof(size_t) + dwLen.value() * sizeof(WCHAR));
            break;
        case ColumnType::UTF8Type:
            if (dwMaxLen.has_value())
                ZeroMemory(boundData.AString, sizeof(size_t) + dwMaxLen.value() * sizeof(CHAR));
            else if (dwLen.has_value())
                ZeroMemory(boundData.AString, sizeof(size_t) + dwLen.value() * sizeof(CHAR));
            break;
        case ColumnType::BinaryType:
            if (dwMaxLen.has_value())
                ZeroMemory(boundData.Binary, dwMaxLen.value() + sizeof(size_t));
            else if (dwLen.has_value())
                ZeroMemory(boundData.Binary, sizeof(size_t) + dwLen.value() * sizeof(CHAR));
            break;
    }
    return S_OK;
}
HRESULT BoundColumn::PrintToBuffer(const WCHAR* szFormat, ...)
{
    HRESULT hr = E_FAIL;

    va_list argList;
    va_start(argList, szFormat);

    hr = PrintToBuffer(szFormat, argList);

    va_end(argList);
    return hr;
}

HRESULT BoundColumn::PrintToBuffer(const WCHAR* szFormat, va_list argList)
{
    HRESULT hr = E_FAIL;

    size_t dwRemaining = 0L;

    switch (Type)
    {
        case UTF16Type:
            if (SUCCEEDED(
                    hr = StringCbVPrintfExW(
                        (LPWSTR)boundData.WString->Data,
                        (size_t)dwMaxLen.value() * sizeof(WCHAR),
                        NULL,
                        &dwRemaining,
                        STRSAFE_IGNORE_NULLS,
                        szFormat,
                        argList)))
                boundData.WString->iIndicator = dwMaxLen.value() * sizeof(WCHAR) - dwRemaining;
            else
                return hr;
            break;
        case Nothing:
            break;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteString(const WCHAR* szString)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UTF16Type:
        case XMLType:
            boundData.WString->iIndicator = wcslen(szString) * sizeof(WCHAR);
            if (dwMaxLen.has_value())
            {
                if (boundData.WString->iIndicator > dwMaxLen.value() * sizeof(WCHAR))
                    return E_NOT_SUFFICIENT_BUFFER;
                wcsncpy_s(boundData.WString->Data, dwMaxLen.value(), szString, boundData.WString->iIndicator);
            }
            break;
        case UTF8Type:
            boundData.WString->iIndicator = wcslen(szString) * sizeof(WCHAR);
            if (dwMaxLen.has_value())
            {
                if (boundData.WString->iIndicator > dwMaxLen.value())
                    return E_NOT_SUFFICIENT_BUFFER;
                if (FAILED(hr = WideToAnsi(nullptr, szString, boundData.AString->Data, dwMaxLen.value())))
                    return hr;
            }
            break;
        case BinaryType:
            boundData.Binary->iIndicator = wcslen(szString) * sizeof(WCHAR);
            if (dwMaxLen.has_value())
            {
                if (boundData.Binary->iIndicator > dwMaxLen.value() * sizeof(WCHAR))
                    return E_NOT_SUFFICIENT_BUFFER;
                wcscpy_s((LPWSTR)boundData.Binary->Data, dwMaxLen.value() / sizeof(WCHAR), szString);
            }
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }

    return S_OK;
}

HRESULT BoundColumn::WriteCharArray(const WCHAR* szString, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UTF16Type:
            boundData.WString->iIndicator = dwCharCount * sizeof(WCHAR);
            if (dwMaxLen.has_value())
            {
                if (boundData.WString->iIndicator > dwMaxLen.value() * sizeof(WCHAR))
                    return E_NOT_SUFFICIENT_BUFFER;
                wmemcpy_s(boundData.WString->Data, dwMaxLen.value(), szString, dwCharCount);
            }
            break;
        case XMLType:
            boundData.WString->iIndicator = (dwCharCount - 1) * sizeof(WCHAR);
            if (dwMaxLen.has_value())
            {
                if (boundData.WString->iIndicator > dwMaxLen.value() * sizeof(WCHAR))
                    return E_NOT_SUFFICIENT_BUFFER;
                wmemcpy_s(boundData.WString->Data, dwMaxLen.value(), szString, dwCharCount - 1);
            }
            break;
        case UTF8Type:
            boundData.AString->iIndicator = dwCharCount;
            if (dwMaxLen.has_value())
            {
                if (boundData.AString->iIndicator > dwMaxLen.value())
                    return E_NOT_SUFFICIENT_BUFFER;
                if (FAILED(hr = WideToAnsi(nullptr, szString, boundData.AString->Data, dwMaxLen.value())))
                    return hr;
            }
            break;
        case BinaryType:
            boundData.Binary->iIndicator = dwCharCount * sizeof(WCHAR);
            if (dwMaxLen.has_value())
            {
                if (boundData.Binary->iIndicator > dwMaxLen.value())
                    return E_NOT_SUFFICIENT_BUFFER;
                wmemcpy_s((LPWSTR)boundData.Binary->Data, dwMaxLen.value() / sizeof(WCHAR), szString, dwCharCount);
            }
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::PrintToBuffer(const CHAR* szFormat, ...)
{
    HRESULT hr = E_FAIL;

    va_list argList;
    va_start(argList, szFormat);

    hr = PrintToBuffer(szFormat, argList);

    va_end(argList);
    return hr;
}

HRESULT BoundColumn::PrintToBuffer(const CHAR* szFormat, va_list argList)
{
    HRESULT hr = E_FAIL;

    size_t dwRemaining = 0L;

    switch (Type)
    {
        case UTF16Type:
            return E_NOTIMPL;
        case UTF8Type:
            if (dwMaxLen.has_value())
            {
                if (SUCCEEDED(
                        hr = StringCbVPrintfExA(
                            boundData.AString->Data,
                            dwMaxLen.value() * sizeof(CHAR),
                            NULL,
                            &dwRemaining,
                            STRSAFE_IGNORE_NULLS,
                            szFormat,
                            argList)))
                    boundData.AString->iIndicator = dwMaxLen.value() * sizeof(CHAR) - dwRemaining;
                else
                    return hr;
            }
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteString(const CHAR* szString)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UTF16Type:
        case XMLType:
            boundData.WString->iIndicator = strlen(szString);
            if (FAILED(
                    hr = AnsiToWide(
                        nullptr,
                        szString,
                        (DWORD)boundData.WString->iIndicator,
                        boundData.WString->Data,
                        dwMaxLen.value())))
                return hr;
            break;
        case UTF8Type:
            boundData.WString->iIndicator = strlen(szString);
            strncpy_s(boundData.AString->Data, dwMaxLen.value_or(0L), szString, boundData.WString->iIndicator);
            break;
        case BinaryType:
            boundData.Binary->iIndicator = strlen(szString);
            if (boundData.Binary->iIndicator > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            strcpy_s((LPSTR)boundData.Binary->Data, dwMaxLen.value_or(0L), szString);
            break;
        case Nothing:
            return S_OK;
        default:
            return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteCharArray(const CHAR* szString, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UTF16Type:
        case XMLType:
            boundData.WString->iIndicator = dwCharCount * sizeof(CHAR);
            if (FAILED(
                    hr = AnsiToWide(
                        nullptr,
                        szString,
                        (DWORD)boundData.WString->iIndicator,
                        boundData.WString->Data,
                        dwMaxLen.value())))
                return hr;
            break;
        case UTF8Type:
            boundData.WString->iIndicator = dwCharCount * sizeof(CHAR);
            memcpy_s(boundData.AString->Data, dwMaxLen.value(), szString, boundData.WString->iIndicator);
            break;
        case BinaryType:
            boundData.Binary->iIndicator = dwCharCount * sizeof(CHAR);
            if (boundData.Binary->iIndicator > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            memcpy_s((LPSTR)boundData.Binary->Data, dwMaxLen.value(), szString, boundData.Binary->iIndicator);
            break;
        case Nothing:
            return S_OK;
        default:
            return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT BoundColumn::WriteAttributes(DWORD dwAttributes)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UInt32Type:
            boundData.Dword = dwAttributes;
            break;
        case UTF16Type:
            if (FAILED(
                    hr = PrintToBuffer(
                        L"%c%c%c%c%c%c%c%c%c%c%c%c%c",
                        dwAttributes & FILE_ATTRIBUTE_ARCHIVE ? L'A' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_COMPRESSED ? L'C' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_DIRECTORY ? L'D' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_ENCRYPTED ? L'E' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_HIDDEN ? L'H' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_NORMAL ? L'N' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_OFFLINE ? L'O' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_READONLY ? L'R' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? L'L' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? L'P' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_SYSTEM ? L'S' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_TEMPORARY ? L'T' : L'.',
                        dwAttributes & FILE_ATTRIBUTE_VIRTUAL ? L'V' : L'.')))
            {
                return hr;
            }
            break;
        case UTF8Type:
            if (FAILED(
                    hr = PrintToBuffer(
                        "%c%c%c%c%c%c%c%c%c%c%c%c%c",
                        dwAttributes & FILE_ATTRIBUTE_ARCHIVE ? 'A' : '.',
                        dwAttributes & FILE_ATTRIBUTE_COMPRESSED ? 'C' : '.',
                        dwAttributes & FILE_ATTRIBUTE_DIRECTORY ? 'D' : '.',
                        dwAttributes & FILE_ATTRIBUTE_ENCRYPTED ? 'E' : '.',
                        dwAttributes & FILE_ATTRIBUTE_HIDDEN ? 'H' : '.',
                        dwAttributes & FILE_ATTRIBUTE_NORMAL ? 'N' : '.',
                        dwAttributes & FILE_ATTRIBUTE_OFFLINE ? 'O' : '.',
                        dwAttributes & FILE_ATTRIBUTE_READONLY ? 'R' : '.',
                        dwAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? '?' : '.',
                        dwAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? '?' : '.',
                        dwAttributes & FILE_ATTRIBUTE_SYSTEM ? 'S' : '.',
                        dwAttributes & FILE_ATTRIBUTE_TEMPORARY ? 'T' : '.',
                        dwAttributes & FILE_ATTRIBUTE_VIRTUAL ? 'V' : '.')))
            {
                return hr;
            }
            break;
        case BinaryType:
            if ((sizeof(DWORD) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(DWORD);
            *((DWORD*)boundData.Binary->Data) = dwAttributes;
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::FileTimeToDBTime(FILETIME& FileTime, Orc::TableOutput::DBTIMESTAMP& DBTime)
{
    SYSTEMTIME stUTC;
    if (!FileTimeToSystemTime(&FileTime, &stUTC))
        return HRESULT_FROM_WIN32(GetLastError());

    DBTime.year = stUTC.wYear;
    DBTime.month = stUTC.wMonth;
    DBTime.day = stUTC.wDay;

    DBTime.hour = stUTC.wHour;
    DBTime.minute = stUTC.wMinute;
    DBTime.second = stUTC.wSecond;

    DBTime.fraction = stUTC.wMilliseconds * 1000 * 1000;
    return S_OK;
}

HRESULT BoundColumn::WriteFileTime(FILETIME fileTime)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::TimeStampType:
            if (FAILED(hr = FileTimeToDBTime(fileTime, boundData.TimeStamp)))
                return hr;
            break;
        case ColumnType::UTF16Type:
        {
            // Convert the Create time to System time.
            SYSTEMTIME stUTC;
            FileTimeToSystemTime(&fileTime, &stUTC);

            if (FAILED(
                    hr = PrintToBuffer(
                        L"%u-%02u-%02u %02u:%02u:%02u.%03u%s",
                        stUTC.wYear,
                        stUTC.wMonth,
                        stUTC.wDay,
                        stUTC.wHour,
                        stUTC.wMinute,
                        stUTC.wSecond,
                        stUTC.wMilliseconds)))
                return hr;
        }
        break;
        case ColumnType::UTF8Type:
        {
            // Convert the Create time to System time.
            SYSTEMTIME stUTC;
            FileTimeToSystemTime(&fileTime, &stUTC);

            if (FAILED(
                    hr = PrintToBuffer(
                        "%u-%02u-%02u %02u:%02u:%02u.%03u%s",
                        stUTC.wYear,
                        stUTC.wMonth,
                        stUTC.wDay,
                        stUTC.wHour,
                        stUTC.wMinute,
                        stUTC.wSecond,
                        stUTC.wMilliseconds)))
                return hr;
        }
        break;
        case ColumnType::BinaryType:
            if ((sizeof(FILETIME) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(FILETIME);
            *((FILETIME*)boundData.Binary->Data) = fileTime;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteFileTime(LONGLONG fileTime)
{
    HRESULT hr = E_FAIL;

    LARGE_INTEGER li;
    li.QuadPart = fileTime;

    FILETIME ft = {(DWORD)li.u.LowPart, (DWORD)li.u.HighPart};

    switch (Type)
    {
        case ColumnType::TimeStampType:
            if (FAILED(hr = FileTimeToDBTime(ft, boundData.TimeStamp)))
                return hr;
            break;
        case ColumnType::UTF16Type:
        {
            // Convert the Create time to System time.
            SYSTEMTIME stUTC;
            FileTimeToSystemTime(&ft, &stUTC);

            if (FAILED(
                    hr = PrintToBuffer(
                        L"%u-%02u-%02u %02u:%02u:%02u.%03u%s",
                        stUTC.wYear,
                        stUTC.wMonth,
                        stUTC.wDay,
                        stUTC.wHour,
                        stUTC.wMinute,
                        stUTC.wSecond,
                        stUTC.wMilliseconds)))
                return hr;
        }
        break;
        case ColumnType::UTF8Type:
        {
            // Convert the Create time to System time.
            SYSTEMTIME stUTC;
            FileTimeToSystemTime(&ft, &stUTC);

            if (FAILED(
                    hr = PrintToBuffer(
                        "%u-%02u-%02u %02u:%02u:%02u.%03u%s",
                        stUTC.wYear,
                        stUTC.wMonth,
                        stUTC.wDay,
                        stUTC.wHour,
                        stUTC.wMinute,
                        stUTC.wSecond,
                        stUTC.wMilliseconds)))
                return hr;
        }
        break;
        case ColumnType::BinaryType:
            if ((sizeof(FILETIME) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(FILETIME);
            *((FILETIME*)boundData.Binary->Data) = ft;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteFileSize(LARGE_INTEGER fileSize)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::UInt64Type:
            boundData.LargeInt = fileSize;
            break;
        case ColumnType::UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"%I64d", fileSize.QuadPart)))
                return hr;
            break;
        case ColumnType::UTF8Type:
            if (FAILED(hr = PrintToBuffer("%I64d", fileSize.QuadPart)))
                return hr;
            break;
        case ColumnType::BinaryType:
            if ((sizeof(LARGE_INTEGER) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(LARGE_INTEGER);
            *((LARGE_INTEGER*)boundData.Binary->Data) = fileSize;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteFileSize(ULONGLONG fileSize)
{
    HRESULT hr = E_FAIL;
    switch (Type)
    {
        case ColumnType::UInt64Type:
            boundData.LargeInt.QuadPart = fileSize;
            break;
        case ColumnType::UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"%I64d", fileSize)))
                return hr;
            break;
        case ColumnType::UTF8Type:
            if (FAILED(hr = PrintToBuffer("%I64d", fileSize)))
                return hr;
            break;
        case ColumnType::BinaryType:
            if ((sizeof(ULONGLONG) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(ULONGLONG);
            *((ULONGLONG*)boundData.Binary->Data) = fileSize;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::UInt64Type:
            boundData.LargeInt.HighPart = nFileSizeHigh;
            boundData.LargeInt.LowPart = nFileSizeLow;
            break;
        case ColumnType::UTF16Type:
        {
            LARGE_INTEGER li;
            li.HighPart = nFileSizeHigh;
            li.LowPart = nFileSizeLow;
            if (FAILED(hr = PrintToBuffer(L"%I64d", li.QuadPart)))
                return hr;
        }
        break;
        case ColumnType::UTF8Type:
        {
            LARGE_INTEGER li;
            li.HighPart = nFileSizeHigh;
            li.LowPart = nFileSizeLow;
            if (FAILED(hr = PrintToBuffer("%I64d", li.QuadPart)))
                return hr;
        }
        break;
        case ColumnType::BinaryType:
        {
            LARGE_INTEGER li;
            li.HighPart = nFileSizeHigh;
            li.LowPart = nFileSizeLow;
            if ((sizeof(LARGE_INTEGER) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(LARGE_INTEGER);
            *((LARGE_INTEGER*)boundData.Binary->Data) = li;
        }
        break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteInteger(DWORD dwInteger)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::UInt32Type:
            boundData.Dword = dwInteger;
            break;
        case ColumnType::UInt64Type:
            boundData.LargeInt.HighPart = 0L;
            boundData.LargeInt.LowPart = dwInteger;
            break;
        case ColumnType::UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"%d", dwInteger)))
                return hr;
            return hr;
        case ColumnType::UTF8Type:
            if (FAILED(hr = PrintToBuffer("%d", dwInteger)))
                return hr;
            return hr;
        case ColumnType::BinaryType:
            if ((sizeof(DWORD) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(DWORD);
            *((DWORD*)boundData.Binary->Data) = dwInteger;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteInteger(LONGLONG dw64Integer)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::UInt32Type:
            boundData.LargeInt.QuadPart = dw64Integer;
            if (boundData.LargeInt.HighPart != 0L)
                return E_INVALIDARG;
            boundData.Dword = boundData.LargeInt.LowPart;
            break;
        case ColumnType::UInt64Type:
            boundData.LargeInt.QuadPart = dw64Integer;
            break;
        case ColumnType::UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"%I64d", dw64Integer)))
                return hr;
            break;
        case ColumnType::UTF8Type:
            if (FAILED(hr = PrintToBuffer("%I64d", dw64Integer)))
                return hr;
            break;
        case ColumnType::BinaryType:
            if ((sizeof(LONGLONG) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(LONGLONG);
            *((LONGLONG*)boundData.Binary->Data) = dw64Integer;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteInteger(ULONGLONG dw64Integer)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::UInt32Type:
            boundData.LargeInt.QuadPart = dw64Integer;
            if (boundData.LargeInt.HighPart != 0L)
                return E_INVALIDARG;
            boundData.Dword = boundData.LargeInt.LowPart;
            break;
        case ColumnType::UInt64Type:
            boundData.LargeInt.QuadPart = dw64Integer;
            break;
        case ColumnType::UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"%I64d", dw64Integer)))
                return hr;
            return hr;
        case ColumnType::UTF8Type:
            if (FAILED(hr = PrintToBuffer("%I64d", dw64Integer)))
                return hr;
            return hr;
        case ColumnType::BinaryType:
            if ((sizeof(ULONGLONG) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(ULONGLONG);
            *((ULONGLONG*)boundData.Binary->Data) = dw64Integer;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteIntegerInHex(DWORD dwInteger)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case ColumnType::UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"0x%lX", dwInteger)))
                return hr;
            break;
        case ColumnType::UTF8Type:
            if (FAILED(hr = PrintToBuffer("0x%lX", dwInteger)))
                return hr;
            break;
        case ColumnType::UInt32Type:
            boundData.Dword = dwInteger;
            break;
        case ColumnType::BinaryType:
            if ((sizeof(DWORD) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(DWORD);
            *((DWORD*)boundData.Binary->Data) = dwInteger;
            break;
        case ColumnType::Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteIntegerInHex(DWORDLONG dwlInteger)
{
    HRESULT hr = E_FAIL;
    switch (Type)
    {
        case UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"0x%lX", dwlInteger)))
                return hr;
            break;
        case UTF8Type:
            if (FAILED(hr = PrintToBuffer("0x%lX", dwlInteger)))
                return hr;
            break;
        case UInt64Type:
            boundData.LargeInt.QuadPart = dwlInteger;
            break;
        case BinaryType:
            if ((sizeof(DWORDLONG) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(DWORDLONG);
            *((DWORDLONG*)boundData.Binary->Data) = dwlInteger;
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteIntegerInHex(LONGLONG dwlInteger)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UTF16Type:
            if (FAILED(hr = PrintToBuffer(L"0x%lX", dwlInteger)))
                return hr;
            break;
        case UTF8Type:
            if (FAILED(hr = PrintToBuffer("0x%lX", dwlInteger)))
                return hr;
            break;
        case UInt64Type:
            boundData.LargeInt.QuadPart = dwlInteger;
            break;
        case BinaryType:
            if ((sizeof(LONGLONG) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(LONGLONG);
            *((LONGLONG*)boundData.Binary->Data) = dwlInteger;
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
HRESULT BoundColumn::WriteBytesInHex(const BYTE pBytes[], DWORD dwLen)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case BinaryType:
            if (dwMaxLen < dwLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = dwLen;
            memcpy_s(boundData.Binary->Data, dwMaxLen.value(), pBytes, dwLen);
            break;
        case UTF16Type:
        {
            LPWSTR pCur = boundData.WString->Data;
            size_t dwBytesLeft = dwMaxLen.value() * sizeof(WCHAR);  // StringCbPrintfExW counts bytes

            if (dwBytesLeft < (dwLen * 2 * sizeof(WCHAR)))
                return E_NOT_SUFFICIENT_BUFFER;

            for (DWORD i = 0; i < dwLen; i++)
            {
                if (FAILED(
                        hr = StringCbPrintfExW(
                            pCur, dwBytesLeft, &pCur, &dwBytesLeft, STRSAFE_IGNORE_NULLS, L"%2.2X", pBytes[i])))
                    return hr;
            }
            boundData.WString->iIndicator = (dwMaxLen.value() * sizeof(WCHAR)) - dwBytesLeft;
        }
        break;
        case UTF8Type:
        {
            LPSTR pCur = boundData.AString->Data;
            size_t dwBytesLeft = dwMaxLen.value() * sizeof(CHAR);  // StringCbPrintfExW counts bytes

            if (dwBytesLeft < (dwLen * 2 * sizeof(CHAR)))
                return E_NOT_SUFFICIENT_BUFFER;

            for (DWORD i = 0; i < dwLen; i++)
            {
                if (FAILED(
                        hr = StringCbPrintfExA(
                            pCur, dwBytesLeft, &pCur, &dwBytesLeft, STRSAFE_IGNORE_NULLS, "%2.2X", pBytes[i])))
                    return hr;
            }
            boundData.AString->iIndicator = (dwMaxLen.value() * sizeof(CHAR)) - dwBytesLeft;
        }
        break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
HRESULT BoundColumn::WriteBool(bool bBoolean)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case BoolType:
            boundData.Boolean = bBoolean;
            break;
        case UTF16Type:
            if (FAILED(hr = WriteString(bBoolean ? L"true" : L"false")))
                return hr;
            break;
        case UTF8Type:
            if (FAILED(hr = WriteString(bBoolean ? "true" : "false")))
                return hr;
            break;
        case BinaryType:
            if ((sizeof(bool) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(bool);
            *((bool*)boundData.Binary->Data) = bBoolean;
            break;
        case Nothing:
            break;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
HRESULT BoundColumn::WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[])
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UInt32Type:
            boundData.Dword = dwEnum;
            break;
        case UInt64Type:
            boundData.LargeInt.HighPart = 0L;
            boundData.LargeInt.LowPart = dwEnum;
            break;
        case UTF16Type:
        {
            unsigned int i = 0;
            const WCHAR* szValue = NULL;

            while (((szValue = EnumValues[i]) != NULL) && i < dwEnum)
                i++;

            if (szValue == NULL)
                szValue = L"IllegalEnumValue";

            if (wcslen(szValue) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.WString->iIndicator = wcslen(szValue) * sizeof(WCHAR);
            wcsncpy_s(((LPWSTR)boundData.WString->Data), dwMaxLen.value(), szValue, wcslen(szValue));
        }
        break;
        case UTF8Type:
        {
            unsigned int i = 0;
            const WCHAR* szValue = NULL;

            while (((szValue = EnumValues[i]) != NULL) && i < dwEnum)
                i++;

            if (szValue == NULL)
                szValue = L"IllegalEnumValue";

            if (wcslen(szValue) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.AString->iIndicator = wcslen(szValue) * sizeof(CHAR);
            if (FAILED(hr = WideToAnsi(nullptr, szValue, boundData.AString->Data, dwMaxLen.value())))
                return hr;
        }
        break;
        case BinaryType:
            if ((sizeof(DWORD) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(DWORD);
            *((DWORD*)boundData.Binary->Data) = dwEnum;
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UInt32Type:
            boundData.Dword = dwFlags;
            break;
        case UInt64Type:
            boundData.LargeInt.HighPart = 0L;
            boundData.LargeInt.LowPart = dwFlags;
            break;
        case UTF16Type:
        {
            bool bFirst = true;
            int idx = 0;
            LPWSTR pCur = boundData.WString->Data;
            size_t dwBytesLeft = dwMaxLen.value() * sizeof(WCHAR);

            while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
            {
                if (dwFlags & FlagValues[idx].dwFlag)
                {
                    if (bFirst)
                    {
                        bFirst = false;
                        if (FAILED(
                                hr = StringCbPrintfExW(
                                    pCur,
                                    dwBytesLeft,
                                    &pCur,
                                    &dwBytesLeft,
                                    STRSAFE_IGNORE_NULLS,
                                    L"%s",
                                    FlagValues[idx].szShortDescr)))
                            return hr;
                    }
                    else
                    {
                        if (FAILED(
                                hr = StringCbPrintfExW(
                                    pCur,
                                    dwBytesLeft,
                                    &pCur,
                                    &dwBytesLeft,
                                    STRSAFE_IGNORE_NULLS,
                                    L"%c%s",
                                    cSeparator,
                                    FlagValues[idx].szShortDescr)))
                            return hr;
                    }
                }
                idx++;
            }
            boundData.WString->iIndicator = dwMaxLen.value() * sizeof(WCHAR) - dwBytesLeft;
        }
        break;
        case UTF8Type:
        {
            bool bFirst = true;
            int idx = 0;
            LPSTR pCur = boundData.AString->Data;
            size_t dwBytesLeft = dwMaxLen.value();
            CHAR szBuf[MAX_PATH];

            WCHAR wszSep[2] = {cSeparator, '\0'};
            CHAR szSep[2];
            if (FAILED(WideToAnsi(nullptr, wszSep, szSep, 2)))
                return hr;

            while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
            {
                if (dwFlags & FlagValues[idx].dwFlag)
                {
                    if (FAILED(WideToAnsi(nullptr, FlagValues[idx].szShortDescr, szBuf, MAX_PATH)))
                        return hr;
                    if (bFirst)
                    {
                        bFirst = false;
                        if (FAILED(
                                hr = StringCbPrintfExA(
                                    pCur, dwBytesLeft, &pCur, &dwBytesLeft, STRSAFE_IGNORE_NULLS, "%s", szBuf)))
                            return hr;
                    }
                    else
                    {
                        if (FAILED(
                                hr = StringCbPrintfExA(
                                    pCur,
                                    dwBytesLeft,
                                    &pCur,
                                    &dwBytesLeft,
                                    STRSAFE_IGNORE_NULLS,
                                    "%c%s",
                                    szSep[0],
                                    szBuf)))
                            return hr;
                    }
                }
                idx++;
            }
            boundData.AString->iIndicator = dwMaxLen.value() * sizeof(CHAR) - dwBytesLeft;
        }
        break;
        case BinaryType:
            if (dwMaxLen.has_value())
            {
                if ((sizeof(DWORD) + sizeof(size_t)) > dwMaxLen.value())
                    return E_NOT_SUFFICIENT_BUFFER;
                boundData.Binary->iIndicator = sizeof(DWORD);
                *((DWORD*)boundData.Binary->Data) = dwFlags;
            }
            else if (dwLen.has_value())
            {
                if ((sizeof(DWORD) + sizeof(size_t)) > dwLen)
                    return E_NOT_SUFFICIENT_BUFFER;
                boundData.Binary->iIndicator = sizeof(DWORD);
                *((DWORD*)boundData.Binary->Data) = dwFlags;
            }
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
HRESULT BoundColumn::WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
        case UInt32Type:
            boundData.Dword = dwFlags;
            break;
        case UInt64Type:
            boundData.LargeInt.HighPart = 0L;
            boundData.LargeInt.LowPart = dwFlags;
            break;
        case UTF16Type:
        {
            int idx = 0;
            LPWSTR pCur = boundData.WString->Data;
            size_t dwBytesLeft = dwMaxLen.value() * sizeof(WCHAR);

            while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
            {
                if (dwFlags == FlagValues[idx].dwFlag)
                {
                    if (FAILED(
                            hr = StringCbPrintfExW(
                                pCur,
                                dwBytesLeft,
                                &pCur,
                                &dwBytesLeft,
                                STRSAFE_IGNORE_NULLS,
                                L"%s",
                                FlagValues[idx].szShortDescr)))
                        return hr;
                }
                idx++;
            }
            boundData.AString->iIndicator = dwMaxLen.value() * sizeof(WCHAR) - dwBytesLeft;
        }
        break;
        case UTF8Type:
        {
            int idx = 0;
            LPSTR pCur = boundData.AString->Data;
            size_t dwBytesLeft = dwMaxLen.value() * sizeof(CHAR);

            CHAR szBuf[MAX_PATH];

            while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
            {
                if (FAILED(WideToAnsi(nullptr, FlagValues[idx].szShortDescr, szBuf, MAX_PATH)))
                    return hr;
                if (dwFlags == FlagValues[idx].dwFlag)
                {
                    if (FAILED(
                            hr = StringCbPrintfExA(
                                pCur, dwBytesLeft, &pCur, &dwBytesLeft, STRSAFE_IGNORE_NULLS, "%s", szBuf)))
                        return hr;
                }
                idx++;
            }
            boundData.AString->iIndicator = dwMaxLen.value() * sizeof(CHAR) - dwBytesLeft;
        }
        break;
        case BinaryType:
            if (dwMaxLen.has_value())
            {
                if ((sizeof(DWORD) + sizeof(size_t)) > dwMaxLen.value())
                    return E_NOT_SUFFICIENT_BUFFER;
                boundData.Binary->iIndicator = sizeof(DWORD);
                *((DWORD*)boundData.Binary->Data) = dwFlags;
            }
            else if (dwLen.has_value())
            {
                if ((sizeof(DWORD) + sizeof(size_t)) > dwLen.value())
                    return E_NOT_SUFFICIENT_BUFFER;
                boundData.Binary->iIndicator = sizeof(DWORD);
                *((DWORD*)boundData.Binary->Data) = dwFlags;
            }
            break;
        case Nothing:
            return S_OK;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT BoundColumn::WriteGUID(const GUID& guid)
{
    HRESULT hr = E_FAIL;
    WCHAR szCLSID[MAX_GUID_STRLEN];

    switch (Type)
    {
        case UTF16Type:
            if (!StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
                return E_NOT_SUFFICIENT_BUFFER;
            if (FAILED(hr = PrintToBuffer(L"%s", szCLSID)))
                return hr;
            break;
        case UTF8Type:
            if (!StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
                return E_NOT_SUFFICIENT_BUFFER;
            if (FAILED(hr = PrintToBuffer(L"%S", szCLSID)))
                return hr;
            break;
        case BinaryType:
            if ((sizeof(GUID) + sizeof(size_t)) > dwMaxLen)
                return E_NOT_SUFFICIENT_BUFFER;
            boundData.Binary->iIndicator = sizeof(GUID);
            *((GUID*)boundData.Binary->Data) = guid;
            break;
        case GUIDType:
            boundData.GUID = guid;
            break;
        case Nothing:
            break;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
