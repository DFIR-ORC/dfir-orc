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

class FileReader
{

public:
    enum ColumnType
    {
        UnknownType = 0,
        String,
        Integer,
        LargeInteger,
        DateTime,
        GUIDType,
        Boolean
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
            GUID guid;
            bool boolean;
        };
        Column() { Definition = nullptr; }
    };

    class Record
    {
    public:
        // SourceInformation Source;
        std::vector<Column> Values;
        ULONGLONG ullLineNumber = 0LL;
    };

public:
    FileReader();

    HRESULT SetDateFormat(const WCHAR* szDateFormat = L"yyyy-MM-dd hh:mm:ss.000");
    HRESULT SetBooleanFormat(const WCHAR* szBooleanFormat);

    HRESULT OpenFile(
        const WCHAR* szFileName,
        bool bfirstRowIsColumnNames = true,
        WCHAR wcSeparator = L',',
        WCHAR wcQuote = L'\"',
        const WCHAR* szDateFormat = L"yyyy-MM-dd hh:mm:ss.000",
        LPCWSTR szBool = L"YN",
        DWORD dwSkipLines = 0);
    HRESULT OpenStream(
        const std::shared_ptr<ByteStream>& pByteStream,
        bool bfirstRowIsColumnNames = true,
        WCHAR wcSeparator = L',',
        WCHAR wcQuote = L'\"',
        const WCHAR* szDateFormat = L"yyyy-MM-dd hh:mm:ss.000",
        LPCWSTR szBool = L"YN",
        DWORD dwSkipLines = 0);

    bool IsFileOpened() { return m_pStream != nullptr; }

    OutputSpec::Encoding GetEncoding() const { return m_csvEncoding; };
    const std::wstring& GetFileName() const { return m_strFileName; };
    ULONGLONG GetCurrentLine() const { return m_ullCurLine; };

    // The column headers and types guessing game
    HRESULT PeekHeaders(const WCHAR* szHeadersFileName = NULL);
    HRESULT PeekTypes(DWORD cbLinesToPeek = 20);

    // Setting the schema explicitely
    HRESULT SetSchema(const TableOutput::Schema& columns);

    HRESULT SkipHeaders();

    // The pointers to data returned by the ParseNextLine in the form of the CSVRecord
    // are only valid until the next call to ParseNextLine.
    // you _have_ to make copies of any "String" data value returned before you call ParseNextLine again
    HRESULT ParseNextLine(Record& record);

    const RecordSchema& GetSchema() const { return m_Schema; };

    ~FileReader(void);

private:
    RecordSchema m_Schema;
    std::wstring m_strFileName;
    bool m_bfirstRowIsColumnNames;
    WCHAR m_wcSeparator;
    WCHAR m_wcQuote;
    WCHAR m_BoolChar[2];
    std::wstring m_strDateFormat;
    DWORD m_dwSkipLines;

    OutputSpec::Encoding m_csvEncoding;

    std::shared_ptr<ByteStream> m_pStream;

    CircularStorage m_Store;
    LPBYTE m_pUTF8Buffer;

    LARGE_INTEGER m_liFileSize;  // File total size in bytes
    LARGE_INTEGER m_liFilePos;  // Current number of bytes read

    LARGE_INTEGER m_liStoreBytes;  // Current number of bytes read in the store (cumulative)
    LARGE_INTEGER m_liCurPos;  // Current Store position in bytes

    ULONGLONG m_ullCurLine;

    HRESULT ReadMoreData();

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

#pragma managed(pop)
