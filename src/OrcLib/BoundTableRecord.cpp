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

#include "safeint.h"

using namespace Orc;
using namespace Orc::TableOutput;

#ifndef DBMAXCHAR
#define DBMAXCHAR       (8000+1)
#endif

HRESULT BoundColumn::ClearBoundData()
{
    switch (Type)
    {
    case ColumnType::Nothing:
        break;
    case ColumnType::UInt64Type:
        boundData.LargeInt = 0;
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
    case ColumnType::FixedBinaryType:
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

HRESULT BoundColumn::WriteFormatedString(const WCHAR* szFormat, ...)
{
    HRESULT hr = E_FAIL;

    va_list argList;
    va_start(argList, szFormat);

    hr = PrintToBuffer(szFormat, argList);

    va_end(argList);
    return hr;
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
        if (SUCCEEDED(
            hr = StringCbVPrintfExA(
                boundData.AString->Data,
                dwLen.value_or(dwMaxLen.value_or(DBMAXCHAR)) * sizeof(CHAR),
                NULL,
                &dwRemaining,
                STRSAFE_IGNORE_NULLS,
                szFormat,
                argList)))
            boundData.AString->iIndicator = dwLen.value_or(dwMaxLen.value_or(DBMAXCHAR)) * sizeof(CHAR) - dwRemaining;
        else
            return hr;
        break;
    case Nothing:
        return S_OK;
    default:
        return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT Orc::TableOutput::BoundColumn::WriteString(const std::wstring_view& svString)
{
    HRESULT hr = E_FAIL;

    auto dwStrMaxLen = dwLen.value_or(dwMaxLen.value_or(DBMAXCHAR));
    if (svString.size() > dwStrMaxLen)
        return E_NOT_SUFFICIENT_BUFFER;

    switch (Type)
    {
    case UTF16Type:
    case XMLType:
        boundData.WString->iIndicator = svString.size() * sizeof(WCHAR);
        wmemcpy_s(boundData.WString->Data, dwStrMaxLen, svString.data(), svString.size());
        break;
    case UTF8Type:
        boundData.AString->iIndicator = svString.size();
        if (FAILED(hr = WideToAnsi(nullptr, svString.data(), svString.size(), boundData.AString->Data, dwStrMaxLen)))
            return hr;
        break;
    case BinaryType:
        boundData.Binary->iIndicator = svString.size() * sizeof(WCHAR);
        wmemcpy_s((WCHAR*)boundData.Binary->Data, dwStrMaxLen, svString.data(), svString.size());
        break;
    case Nothing:
        return S_OK;
    default:
        return E_NOTIMPL;
    }

    return S_OK;
}

HRESULT Orc::TableOutput::BoundColumn::WriteString(const std::string_view& svString)
{
    HRESULT hr = E_FAIL;

    auto dwStrMaxLen = dwLen.value_or(dwMaxLen.value_or(DBMAXCHAR));
    if (svString.size() > dwStrMaxLen)
        return E_NOT_SUFFICIENT_BUFFER;

    switch (Type)
    {
    case UTF16Type:
    case XMLType:
        boundData.WString->iIndicator = svString.size() * sizeof(WCHAR);
        if (FAILED(
            hr = AnsiToWide(
                nullptr,
                svString.data(),
                svString.size(),
                boundData.WString->Data,
                dwStrMaxLen)))
            return hr;
        break;
    case UTF8Type:
        boundData.AString->iIndicator = svString.size();
        memcpy_s((CHAR*)boundData.AString->Data, dwStrMaxLen, svString.data(), svString.size());
        break;
    case BinaryType:
        boundData.Binary->iIndicator = svString.size();
        memcpy_s((CHAR*)boundData.Binary->Data, dwStrMaxLen, svString.data(), svString.size());
        break;
    case Nothing:
        return S_OK;
    default:
        return E_NOTIMPL;
    }
    return S_OK;
}

HRESULT Orc::TableOutput::BoundColumn::WriteFormated(const std::wstring_view& szFormat, IOutput::wformat_args args)
{
    HRESULT hr = E_FAIL;

    auto dwStrMaxLen = dwLen.value_or(dwMaxLen.value_or(DBMAXCHAR));

    switch (Type)
    {
    case UTF16Type:
    case XMLType:
    {
        Buffer<WCHAR, MAX_PATH> buffer;
        buffer.view_of(boundData.WString->Data, dwMaxLen.value_or(DBMAXCHAR), 0L);
        auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
        boundData.WString->iIndicator = buffer.size() * sizeof(WCHAR);
    }
    break;
    case UTF8Type:
    {
        Buffer<WCHAR, MAX_PATH> buffer;
        auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
        if (buffer.size() > dwMaxLen.value_or(DBMAXCHAR))
            return E_NOT_SUFFICIENT_BUFFER;
        if (FAILED(hr = WideToAnsi(nullptr, (LPWSTR)buffer, boundData.AString->Data, dwMaxLen.value_or(DBMAXCHAR))))
            return hr;
        boundData.AString->iIndicator = buffer.size();
    }
    break;
    case BinaryType:
    {
        Buffer<WCHAR, MAX_PATH> buffer;
        buffer.view_of((WCHAR*)boundData.Binary->Data, dwMaxLen.value_or(DBMAXCHAR), 0L);
        auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
        boundData.Binary->iIndicator = buffer.size() * sizeof(WCHAR);
    }
    break;
    case Nothing:
        return S_OK;
    default:
        return E_NOTIMPL;
    }

    return S_OK;
}

HRESULT Orc::TableOutput::BoundColumn::WriteFormated(const std::string_view& szFormat, IOutput::format_args args)
{
    HRESULT hr = E_FAIL;

    auto dwStrMaxLen = dwLen.value_or(dwMaxLen.value_or(DBMAXCHAR));
    
    switch (Type)
    {
    case UTF16Type:
    case XMLType:
    {
        Buffer<CHAR, MAX_PATH> buffer;
        auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
        if (buffer.size() > dwStrMaxLen)
            return E_NOT_SUFFICIENT_BUFFER;
        if (FAILED(hr = AnsiToWide(nullptr, (LPSTR)buffer, boundData.WString->Data, dwStrMaxLen)))
            return hr;
        boundData.WString->iIndicator = buffer.size()*sizeof(WCHAR);
    }
    break;
    case UTF8Type:
    {
        Buffer<CHAR, MAX_PATH> buffer;
        buffer.view_of(boundData.AString->Data, dwStrMaxLen, 0L);
        auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
        boundData.AString->iIndicator = buffer.size();
    }
    break;
    case BinaryType:
    {
        Buffer<CHAR, MAX_PATH> buffer;
        buffer.view_of((CHAR*)boundData.Binary->Data, dwStrMaxLen, 0L);
        auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);
        boundData.Binary->iIndicator = buffer.size();
    }
    break;
    case Nothing:
        return S_OK;
    default:
        return E_NOTIMPL;
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
    case FixedBinaryType:
        if (sizeof(DWORD) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwAttributes;
        break;
    case BinaryType:
        if (sizeof(DWORD) > dwMaxLen)
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
    case ColumnType::FixedBinaryType:
        if (sizeof(FILETIME) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(FILETIME);
        *((FILETIME*)boundData.Binary->Data) = fileTime;
        break;
    case ColumnType::BinaryType:
        if (sizeof(FILETIME) > dwMaxLen)
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

    FILETIME ft = { (DWORD)li.u.LowPart, (DWORD)li.u.HighPart };

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
    case ColumnType::FixedBinaryType:
        if (sizeof(FILETIME) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(FILETIME);
        *((FILETIME*)boundData.Binary->Data) = ft;
        break;
    case ColumnType::BinaryType:
        if (sizeof(FILETIME) > dwMaxLen)
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
        boundData.LargeInt = fileSize.QuadPart;
        break;
    case ColumnType::UTF16Type:
        if (FAILED(hr = PrintToBuffer(L"%I64d", fileSize.QuadPart)))
            return hr;
        break;
    case ColumnType::UTF8Type:
        if (FAILED(hr = PrintToBuffer("%I64d", fileSize.QuadPart)))
            return hr;
        break;
    case ColumnType::FixedBinaryType:
        if (sizeof(LARGE_INTEGER) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(LARGE_INTEGER);
        *((LARGE_INTEGER*)boundData.Binary->Data) = fileSize;
        break;
    case ColumnType::BinaryType:
        if (sizeof(LARGE_INTEGER) > dwMaxLen)
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
        boundData.LargeInt = fileSize;
        break;
    case ColumnType::UTF16Type:
        if (FAILED(hr = PrintToBuffer(L"%I64d", fileSize)))
            return hr;
        break;
    case ColumnType::UTF8Type:
        if (FAILED(hr = PrintToBuffer("%I64d", fileSize)))
            return hr;
        break;
    case ColumnType::FixedBinaryType:
        if (sizeof(ULONGLONG) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(ULONGLONG);
        *((ULONGLONG*)boundData.Binary->Data) = fileSize;
        break;
    case ColumnType::BinaryType:
        if (sizeof(ULONGLONG) > dwMaxLen)
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
    {
        LARGE_INTEGER li{ 0 };
        li.HighPart = nFileSizeHigh;
        li.LowPart = nFileSizeLow;
        boundData.LargeInt = li.QuadPart;
    }
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
    case ColumnType::FixedBinaryType:
    {
        LARGE_INTEGER li;
        li.HighPart = nFileSizeHigh;
        li.LowPart = nFileSizeLow;
        if (sizeof(LARGE_INTEGER) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(LARGE_INTEGER);
        *((LARGE_INTEGER*)boundData.Binary->Data) = li;
    }
    break;
    case ColumnType::BinaryType:
    {
        LARGE_INTEGER li;
        li.HighPart = nFileSizeHigh;
        li.LowPart = nFileSizeLow;
        if (sizeof(LARGE_INTEGER) > dwMaxLen)
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
    {
        LARGE_INTEGER li{ 0 };
        li.LowPart = dwInteger;
        boundData.LargeInt = li.QuadPart;
    }
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
        if (sizeof(DWORD) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwInteger;
        break;
    case ColumnType::FixedBinaryType:
        if (sizeof(DWORD) > dwMaxLen)
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
    using namespace msl::utilities;

    HRESULT hr = E_FAIL;

    switch (Type)
    {
    case ColumnType::UInt32Type:
        boundData.Dword = SafeInt<DWORD>(dw64Integer);
        break;
    case ColumnType::UInt64Type:
        boundData.LargeInt = dw64Integer;
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
        if (sizeof(LONGLONG) > dwMaxLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(LONGLONG);
        *((LONGLONG*)boundData.Binary->Data) = dw64Integer;
        break;
    case ColumnType::FixedBinaryType:
        if (sizeof(LONGLONG) > dwLen)
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
    using namespace msl::utilities;

    HRESULT hr = E_FAIL;

    switch (Type)
    {
    case ColumnType::UInt32Type:
        boundData.Dword = SafeInt<DWORD>(dw64Integer);
        break;
    case ColumnType::UInt64Type:
        boundData.LargeInt = SafeInt<_int64>(dw64Integer);
        break;
    case ColumnType::UTF16Type:
        if (FAILED(hr = PrintToBuffer(L"%I64d", dw64Integer)))
            return hr;
        return hr;
    case ColumnType::UTF8Type:
        if (FAILED(hr = PrintToBuffer("%I64d", dw64Integer)))
            return hr;
        return hr;
    case ColumnType::FixedBinaryType:
        if (sizeof(ULONGLONG) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(ULONGLONG);
        *((ULONGLONG*)boundData.Binary->Data) = dw64Integer;
        break;
    case ColumnType::BinaryType:
        if (sizeof(ULONGLONG) > dwMaxLen)
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
    case ColumnType::FixedBinaryType:
        if (sizeof(DWORD) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwInteger;
        break;
    case ColumnType::BinaryType:
        if (sizeof(DWORD) > dwMaxLen)
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
    using namespace msl::utilities;

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
        boundData.LargeInt = SafeInt<_int64>(dwlInteger);
        break;
    case FixedBinaryType:
        if (sizeof(DWORDLONG) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORDLONG);
        *((DWORDLONG*)boundData.Binary->Data) = dwlInteger;
        break;
    case BinaryType:
        if (sizeof(DWORDLONG) > dwMaxLen)
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
    using namespace msl::utilities;

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
        boundData.LargeInt = dwlInteger;
        break;
    case BinaryType:
        if (sizeof(LONGLONG) > dwMaxLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(LONGLONG);
        *((LONGLONG*)boundData.Binary->Data) = SafeInt<_int64>(dwlInteger);
        break;
    case FixedBinaryType:
        if (sizeof(LONGLONG) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(LONGLONG);
        *((LONGLONG*)boundData.Binary->Data) = SafeInt<_int64>(dwlInteger);
        break;
    case Nothing:
        return S_OK;
    default:
        return E_NOTIMPL;
    }
    return S_OK;
}
HRESULT BoundColumn::WriteBytesInHex(const BYTE pBytes[], DWORD dwBytesLen)
{
    HRESULT hr = E_FAIL;

    switch (Type)
    {
    case FixedBinaryType:
        if (dwBytesLen > dwLen.value() )
            return E_NOT_SUFFICIENT_BUFFER;
        if (dwBytesLen != dwLen.value())
            return E_INVALIDARG;
        boundData.Binary->iIndicator = dwLen.value();
        memcpy_s(boundData.Binary->Data, dwLen.value(), pBytes, dwBytesLen);
        break;
    case BinaryType:
        if (dwMaxLen.value_or(DBMAXCHAR) < dwBytesLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = dwBytesLen;
        memcpy_s(boundData.Binary->Data, dwMaxLen.value_or(DBMAXCHAR), pBytes, dwBytesLen);
        break;
    case UTF16Type:
    {
        LPWSTR pCur = boundData.WString->Data;
        size_t dwBytesLeft = dwMaxLen.value() * sizeof(WCHAR);  // StringCbPrintfExW counts bytes

        if (dwBytesLeft < (dwBytesLen * 2 * sizeof(WCHAR)))
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

        if (dwBytesLeft < (dwBytesLen * 2 * sizeof(CHAR)))
            return E_NOT_SUFFICIENT_BUFFER;

        for (DWORD i = 0; i < dwBytesLen; i++)
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
    case FixedBinaryType:
        if (sizeof(bool) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(bool);
        *((bool*)boundData.Binary->Data) = bBoolean;
        break;
    case BinaryType:
        if (sizeof(bool) > dwMaxLen)
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
        boundData.LargeInt = dwEnum;
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
    case FixedBinaryType:
        if (sizeof(DWORD) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwEnum;
        break;
    case BinaryType:
        if (sizeof(DWORD) > dwMaxLen)
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
        boundData.LargeInt = dwFlags;
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

        WCHAR wszSep[2] = { cSeparator, '\0' };
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
    case FixedBinaryType:
        if (sizeof(DWORD) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwFlags;
        break;
    case BinaryType:
        if (sizeof(DWORD) > dwMaxLen.value())
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwFlags;
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
        boundData.LargeInt = dwFlags;
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
        if (sizeof(DWORD) > dwMaxLen.value())
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwFlags;
    case FixedBinaryType:
        if (sizeof(DWORD) > dwLen.value())
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(DWORD);
        *((DWORD*)boundData.Binary->Data) = dwFlags;
        break;
    case EnumType:
    case FlagsType:
        boundData.Dword = dwFlags;
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
    case FixedBinaryType:
        if (sizeof(GUID) > dwLen)
            return E_NOT_SUFFICIENT_BUFFER;
        boundData.Binary->iIndicator = sizeof(GUID);
        *((GUID*)boundData.Binary->Data) = guid;
        break;
    case BinaryType:
        if (sizeof(GUID) > dwMaxLen)
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
