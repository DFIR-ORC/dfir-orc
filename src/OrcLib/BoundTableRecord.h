//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "TableOutput.h"

#include "SafeInt.h"

#pragma managed(push, off)

namespace Orc {

namespace TableOutput::Sql {
class Writer;
}

namespace TableOutput {

struct DBTIMESTAMP
{
    SHORT year;
    USHORT month;
    USHORT day;
    USHORT hour;
    USHORT minute;
    USHORT second;
    ULONG fraction;
};

struct NoData
{
    int iIndicator;
};

struct BinaryData
{
    size_t iIndicator;
    BYTE Data[1];
};

struct WStringData
{
    size_t iIndicator;
    WCHAR Data[1];
};

struct AStringData
{
    size_t iIndicator;
    CHAR Data[1];
};

class BoundColumn : public Column
{
    friend class Sql::Writer;

private:
    union
    {
        _int64 LargeInt;
        bool Boolean;
        BYTE Byte;
        SHORT Short;
        DWORD Dword;
        DWORD Enum;
        DWORD Flags;
        GUID GUID;
        DBTIMESTAMP TimeStamp;
        WStringData* WString;
        AStringData* AString;
        BinaryData* Binary;
    } boundData;

public:
    BoundColumn(
        ColumnType type,
        std::wstring_view strName,
        std::wstring_view strDescr = L""sv,
        std::wstring_view strFormat = L""sv,
        std::wstring_view strArtifact = L""sv)
        : Column(type, strName, strDescr, strFormat, strArtifact)
    {
        ZeroMemory(&boundData, sizeof(boundData));
        if (type == ColumnType::TimeStampType)
        {
            boundData.TimeStamp.year = 1;
            boundData.TimeStamp.month = 1;
            boundData.TimeStamp.day = 1;
        }
    };
    BoundColumn() { ZeroMemory(&boundData, sizeof(boundData)); };

    BoundColumn(const BoundColumn& other) = default;
    BoundColumn(BoundColumn&& other) noexcept = default;

    HRESULT PrintToBuffer(const WCHAR* szFormat, ...);
    HRESULT PrintToBuffer(const WCHAR* szFormat, va_list argList);
    HRESULT PrintToBuffer(const CHAR* szFormat, ...);
    HRESULT PrintToBuffer(const CHAR* szFormat, va_list argList);

    HRESULT WriteString(const std::wstring& strString) { return WriteString(std::wstring_view(strString)); }
    HRESULT WriteString(std::wstring_view szString);
    HRESULT WriteString(const WCHAR* szString)
    {
        return WriteCharArray(szString, msl::utilities::SafeInt<DWORD>(wcslen(szString)));
    }
    HRESULT WriteCharArray(const WCHAR* szArray, DWORD dwCharCount)
    {
        return WriteString(std::wstring_view(szArray, dwCharCount));
    }
    HRESULT WriteFormatedString(const WCHAR* szFormat, ...);

    HRESULT WriteString(const std::string& strString) { return WriteString(std::string_view(strString)); }
    HRESULT WriteString(std::string_view strString);
    HRESULT WriteString(const CHAR* szString) { return WriteString(std::string_view(szString, strlen(szString))); }
    HRESULT WriteCharArray(const CHAR* szArray, DWORD dwCharCount)
    {
        return WriteString(std::string_view(szArray, dwCharCount));
    }
    HRESULT WriteFormatedString(const CHAR* szFormat, ...);

    HRESULT WriteFormated(std::wstring_view szFormat, fmt::wformat_args args);
    HRESULT WriteFormated(std::string_view szFormat, fmt::format_args args);

    HRESULT WriteAttributes(DWORD dwAttibutes);
    HRESULT WriteFileTime(FILETIME fileTime);
    HRESULT WriteFileTime(LONGLONG fileTime);
    HRESULT WriteFileSize(LARGE_INTEGER fileSize);
    HRESULT WriteFileSize(ULONGLONG fileSize);
    HRESULT WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow);

    HRESULT WriteInteger(DWORD dwInteger);
    HRESULT WriteInteger(LONGLONG dw64Integer);
    HRESULT WriteInteger(ULONGLONG dw64Integer);

    HRESULT WriteIntegerInHex(DWORD dwInteger);
    HRESULT WriteIntegerInHex(DWORDLONG dwlInteger);
    HRESULT WriteIntegerInHex(LONGLONG dwlInteger);

    HRESULT WriteBytesInHex(const BYTE pSHA1[], DWORD dwLen);
    HRESULT WriteBytesInHex(const CBinaryBuffer& Buffer);

    HRESULT WriteBool(bool bBoolean);

    HRESULT WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[]);
    HRESULT WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator);
    HRESULT WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[]);

    HRESULT WriteGUID(const GUID& guid);

    HRESULT WriteXML(const CHAR* szXML);
    HRESULT WriteXML(const WCHAR* szXML);

    HRESULT ClearBoundData();

    static HRESULT FileTimeToDBTime(FILETIME& FileTime, ::Orc::TableOutput::DBTIMESTAMP& DBTime);
};

}  // namespace TableOutput
using BoundRecord = std::vector<TableOutput::BoundColumn>;
}  // namespace Orc

#pragma managed(pop)
