//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "CircularStorage.h"

#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc {

class ByteStream;

namespace TableOutput::CSV {

class Cruncher
{

public:
    enum ColumnType
    {
        UnknownType = 0,
        String,
        Integer,
        LargeInteger,
        DateTime,
    };

    class ColumnDef
    {
    public:
        ColumnType Type;
        std::wstring Name;
        size_t Index;
    };

    class RecordSchema
    {
    public:
        std::vector<ColumnDef> Column;
    };

    typedef struct _SourceInformation
    {
        const WCHAR* szName;
        ULONGLONG ullLineNumber;
    } SourceInformation;

    typedef struct _StringValue
    {
        WCHAR* szString;
        DWORD dwLength;
    } StringValue;

    class Column
    {
    public:
        ColumnDef* Definition;
        union
        {
            StringValue String;
            DWORD dwInteger;
            LARGE_INTEGER liLargeInteger;
            FILETIME ftDateTime;
        };
        Column() { Definition = nullptr; }
    };

    class Record
    {
    public:
        SourceInformation Source;
        std::vector<Column> Values;

        Record() { Source.ullLineNumber = 0; }
    };

public:
    Cruncher()
    {
        m_liFilePos.QuadPart = 0;
        m_liFileSize.QuadPart = 0;
        m_liStoreBytes.QuadPart = 0;
        m_liCurPos.QuadPart = 0;
    };

    HRESULT Initialize(
        bool bfirstRowIsColumnNames,
        WCHAR wcSeparator,
        WCHAR wcQuote,
        const WCHAR* szDateFormat,
        DWORD dwSkipLines);

    HRESULT SetDateFormat(const WCHAR* szDateFormat);

    OutputSpec::Encoding GetEncoding() const { return m_csvEncoding; };
    ULONGLONG GetCurrentLine() const { return m_ullCurLine; };

    HRESULT AddData(const CBinaryBuffer& data) { return m_Store.PushBytes(data); };

    size_t GetAvailableSize() { return m_Store.GetAvailableSize(); }

    HRESULT PeekHeaders();

    HRESULT SetColumnType(DWORD dwColID, ColumnType type);
    HRESULT AddColumn(DWORD dwColID, ColumnType type, LPCWSTR szColName);
    HRESULT PeekTypes(DWORD cbLinesToPeek = 20);

    // The pointers to data returned by the ParseNextLine in the form of the CSVRecord
    // are only valid until the next call to ParseNextLine.
    // you _have_ to make copies of any "String" data value returned before you call ParseNextLine again
    HRESULT ParseNextLine(Record& record);

    const RecordSchema& GetSchema() const { return m_Schema; };

    ~Cruncher();

protected:
    RecordSchema m_Schema;

    bool m_bfirstRowIsColumnNames = true;
    WCHAR m_wcSeparator = L',';
    WCHAR m_wcQuote = L'\"';
    std::wstring m_strDateFormat = L"yyyy-MM-dd hh:mm:ss.000";
    DWORD m_dwSkipLines = 0L;

    OutputSpec::Encoding m_csvEncoding = OutputSpec::Encoding::kUnknown;

    CircularStorage m_Store;
    LPBYTE m_pUTF8Buffer = NULL;

    LARGE_INTEGER m_liStoreBytes;  // Current number of bytes read in the store (cumulative)
    LARGE_INTEGER m_liCurPos;  // Current Store position in bytes

    LARGE_INTEGER m_liFileSize;  // File total size in bytes
    LARGE_INTEGER m_liFilePos;  // Current number of bytes read

    ULONGLONG m_ullCurLine = 1;

    HRESULT SkipLines();

    inline void ForwardBy(DWORD dwIncrement = 1)
    {
        m_Store.ForwardCursorBy(dwIncrement * sizeof(WCHAR));
        m_liCurPos.QuadPart += dwIncrement * sizeof(WCHAR);
    }

    HRESULT ResetCurPos(WCHAR* pNewCurrentPos);

    HRESULT PeekNextToken(WCHAR*& szToken, DWORD& dwTokenLength, bool& bReachedNewLine, bool& bReachedEndOfFile);

    HRESULT GuessType(WCHAR* szToken, DWORD dwTokenLength, ColumnType& type);

    HRESULT CoerceToColumn(WCHAR* szToken, DWORD dwTokenLength, Column& value);
};
}  // namespace TableOutput::CSV
}  // namespace Orc

constexpr auto CSV_READ_CHUNK_IN_BYTES = 40960;

#pragma managed(pop)
