//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CsvCruncher.h"

#include "ParameterCheck.h"

#include "FileStream.h"

#include "boost\scope_exit.hpp"

using namespace std;

using namespace Orc;

HRESULT Orc::TableOutput::CSV::Cruncher::SetDateFormat(const WCHAR* szDateFormat)
{
    if (m_liCurPos.QuadPart > 0)
        return RPC_E_TOO_LATE;
    if (m_strDateFormat.empty())
        return E_INVALIDARG;

    m_strDateFormat.clear();
    m_strDateFormat.assign(szDateFormat);

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::Initialize(
    bool bfirstRowIsColumnNames,
    WCHAR wcSeparator,
    WCHAR wcQuote,
    const WCHAR* szDateFormat,
    DWORD dwSkipLines)
{
    HRESULT hr = E_FAIL;

    m_wcSeparator = wcSeparator;
    m_wcQuote = wcQuote;
    m_strDateFormat.assign(szDateFormat);
    m_dwSkipLines = dwSkipLines;
    m_bfirstRowIsColumnNames = bfirstRowIsColumnNames;

    if (FAILED(hr = m_Store.InitializeStorage(CSV_READ_CHUNK_IN_BYTES * 20, CSV_READ_CHUNK_IN_BYTES * 2)))
        return hr;

    m_liFilePos.QuadPart = 0;
    m_liFileSize.QuadPart = 0;
    m_liCurPos.QuadPart = 0;
    m_ullCurLine = 1;

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock(CSV_READ_CHUNK_IN_BYTES * 2)))
        return hr;
    if (FAILED(hr = m_Store.Unlock()))
        return hr;

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::ResetCurPos(WCHAR* pNewCurPos)
{
    HRESULT hr = E_FAIL;
    WCHAR* pOldPos = (WCHAR*)m_Store.Cursor();

    if (FAILED(hr = m_Store.ResetCursor((LPBYTE)pNewCurPos)))
        return hr;

    DWORDLONG dwPosChange = (LPBYTE)pOldPos - (LPBYTE)pNewCurPos;
    m_liCurPos.QuadPart -= dwPosChange;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::PeekNextToken(
    WCHAR*& szToken,
    DWORD& dwTokenLengthInChars,
    bool& bReachedNewLine,
    bool& bReachedEndOfFile)
{
    if (m_liCurPos.QuadPart >= m_liStoreBytes.QuadPart)
        return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);

    bReachedNewLine = false;
    bReachedEndOfFile = false;

    bool inQuotes = false;

    if (m_Store.Cursor() + 3 * sizeof(WCHAR) >= m_Store.EndOfCursor())
    {
        return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
    }

    WCHAR* OpeningQuote = (WCHAR*)m_Store.Cursor();
    if (*OpeningQuote == m_wcQuote)
    {
        inQuotes = true;
        ForwardBy(1);
    }
    else
        OpeningQuote = nullptr;

    while (m_liCurPos.QuadPart < m_liStoreBytes.QuadPart)
    {
        WCHAR* szTokenStart = (WCHAR*)m_Store.Cursor();

        while (m_Store.Cursor() + 3 * sizeof(WCHAR) < m_Store.EndOfCursor())
        {
            WCHAR* Current = (WCHAR*)m_Store.Cursor();

            if (*Current != m_wcSeparator && !(Current[0] == L'\r' && Current[1] == L'\n'))
            {
                ForwardBy(1);
            }
            else if (Current[0] == L'\r' && Current[1] == L'\n')
            {
                if (inQuotes)
                {
                    // check if quotes were closed
                    if (*(Current - 1) == m_wcQuote && (Current - 1 != OpeningQuote))
                    {
                        // we're closing quote, happy bunny token found.

                        szToken = szTokenStart;
                        dwTokenLengthInChars = (DWORD)(Current - 1 - szToken);

                        if (Current[0] == L'\r' && Current[1] == L'\n')
                            ForwardBy(2);
                        else if (Current[0] == m_wcSeparator && Current[1] == L'\r' && Current[2] == L'\n')
                            ForwardBy(3);
                        else
                        {
                            Log::Warn("Invalid line termination");
                        }

                        *(Current - 1) = L'\0';
                        bReachedNewLine = true;
                        return S_OK;
                    }
                    else
                    {
                        // The separator (,) was found but not after a quote (")
                        // The separator was inside the quotes and must be ignored.
                        ForwardBy(1);
                    }
                }
                else
                {
                    // we're done with the Token
                    szToken = szTokenStart;

                    dwTokenLengthInChars = (DWORD)(Current - szToken);

                    if (Current[0] == L'\r' && Current[1] == L'\n')
                        ForwardBy(2);
                    else if (Current[0] == m_wcSeparator && Current[1] == L'\r' && Current[2] == L'\n')
                        ForwardBy(3);
                    else
                    {
                        Log::Warn("Invalid end of token");
                    }
                    *(Current) = L'\0';
                    bReachedNewLine = true;

                    return S_OK;
                }
            }
            else
            {
                if (inQuotes)
                {
                    // check if quotes were closed
                    if (*(Current - 1) == m_wcQuote && (Current - 1 != OpeningQuote))
                    {
                        // we're closing quote, happy bunny token found.
                        *(Current - 1) = L'\0';
                        szToken = szTokenStart;
                        dwTokenLengthInChars = (DWORD)(Current - 1 - szToken);
                        ForwardBy(1);

                        return S_OK;
                    }
                    else
                    {
                        // The separator (,) was found but not after a quote (")
                        // The separator was inside the quotes and must be ignored.
                        ForwardBy(1);
                    }
                }
                else
                {
                    // we're done with the Token
                    szToken = szTokenStart;
                    *(Current) = L'\0';
                    dwTokenLengthInChars = (DWORD)(Current - szToken);

                    ForwardBy(1);

                    return S_OK;
                }
            }
        }

        // Testing  EOF condition
        if (m_liCurPos.QuadPart + (LONGLONG)(3 * sizeof(WCHAR)) < m_liStoreBytes.QuadPart)
        {
            // falling here we need more data to continue parsing --> we need to restart parsing
            return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
        }
        else
        {
            while (m_Store.Cursor() < m_Store.EndOfCursor())
            {
                WCHAR* Current = (WCHAR*)m_Store.Cursor();

                if (Current[0] != m_wcSeparator && Current[0] != L'\r' && Current[0] != L'\n')
                {
                    ForwardBy(1);
                }
                else
                {
                    // we're done with the Token
                    szToken = szTokenStart;

                    if (Current[0] == L'\r' || Current[0] == L'\n')
                    {
                        bReachedNewLine = true;
                        bReachedEndOfFile = true;
                    }

                    *(Current) = L'\0';
                    dwTokenLengthInChars = (DWORD)(Current - szToken);

                    ForwardBy(1);
                    return S_OK;
                }
            }
        }
    }

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::SkipLines()
{
    DWORD dwLinesSkipped = 0L;
    if (m_dwSkipLines > 0)
    {
        while (m_liCurPos.QuadPart < m_liStoreBytes.QuadPart)
        {
            WCHAR* Current = (WCHAR*)m_Store.Cursor();
            WCHAR* EndOfData = (WCHAR*)m_Store.EndOfCursor();
            while (Current < EndOfData)
            {
                if (!(Current[0] == L'\r' && Current[1] == L'\n'))
                {
                    ForwardBy(1);
                }
                else
                {
                    dwLinesSkipped++;
                    m_ullCurLine++;
                    ForwardBy(2);
                    if (dwLinesSkipped >= m_dwSkipLines)
                        return S_OK;
                }
            }

            return ERROR_NO_MORE_ITEMS;
        }
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::SetColumnType(DWORD dwColID, ColumnType type)
{
    if (m_Schema.Column.size() <= dwColID)
        return E_INVALIDARG;

    m_Schema.Column[dwColID].Type = type;

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::AddColumn(DWORD dwColID, ColumnType type, LPCWSTR szColName)
{
    ColumnDef aCol = {type, szColName, dwColID};
    m_Schema.Column.push_back(aCol);
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::PeekHeaders()
{
    HRESULT hr = E_FAIL;

    if (!m_bfirstRowIsColumnNames)
    {
        Log::Error("No headers in headers file nor in first line. Unsupported");
        return E_INVALIDARG;
    }

    Cruncher* pHeaderReader = this;

    if (m_Schema.Column.empty())
    {
        ColumnDef Col0 = {UnknownType, L"Column0", 0};
        m_Schema.Column.push_back(std::move(Col0));
    }

    WCHAR* szToken = NULL;
    DWORD dwTokenSize = 0L;
    bool bReachedNewLine = false;
    bool bReachedEndOfFile = false;

    while (SUCCEEDED(hr = pHeaderReader->PeekNextToken(szToken, dwTokenSize, bReachedNewLine, bReachedEndOfFile)))
    {
        ColumnDef aCol;

        aCol.Index = m_Schema.Column.size();
        aCol.Type = UnknownType;
        if (dwTokenSize)
        {
            aCol.Name = szToken;
        }
        m_Schema.Column.push_back(std::move(aCol));

        if (bReachedNewLine)
        {
            m_ullCurLine++;
            break;
        }
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::GuessType(WCHAR* szToken, DWORD dwTokenLength, ColumnType& type)
{
    HRESULT hr = E_FAIL;

    if (dwTokenLength == 0)
    {
        type = String;
        return S_OK;
    }

    if (dwTokenLength >= 2 && szToken[0] == L'0' && szToken[1] == L'x')
    {
        bool bIsHexa = true;
        for (unsigned int i = 2; i < dwTokenLength; i++)
        {
            if (!iswxdigit(szToken[i]))
            {
                bIsHexa = false;
                break;
            }
        }
        if (bIsHexa)
        {
            if (dwTokenLength <= 10)
            {
                type = Integer;
                return S_OK;
            }
            if (dwTokenLength <= 18)
            {
                type = LargeInteger;
                return S_OK;
            }
        }
    }

    {
        bool bIsInteger = true;
        for (unsigned int i = 0; i < dwTokenLength; i++)
        {
            if (!iswdigit(szToken[i]))
            {
                bIsInteger = false;
                break;
            }
        }

        if (bIsInteger)
        {
            type = LargeInteger;
            return S_OK;
        }
    }
    {
        FILETIME result;
        if (!FAILED(hr = GetDateFromString(m_strDateFormat.c_str(), szToken, result)))
        {
            type = DateTime;
            return S_OK;
        }
    }
    type = String;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::CoerceToColumn(WCHAR* szToken, DWORD dwTokenLength, Column& value)
{
    HRESULT hr = E_FAIL;

    if (value.Definition == NULL)
        return E_POINTER;

    switch (value.Definition->Type)
    {
        case String:
            value.String.szString = szToken;
            value.String.dwLength = dwTokenLength;
            break;
        case Integer:
            if (szToken[0] == L'0' && szToken[1] == L'x')
            {
                if (FAILED(hr = GetIntegerFromHexaString(szToken, value.dwInteger)))
                    return hr;
            }
            else
            {
                _set_errno(0L);
                value.dwInteger = _wtoi(szToken);
                if ((value.dwInteger == 0L) && (errno == ERANGE || errno == EINVAL))
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
                }
            }
            break;
        case LargeInteger:
            if (szToken[0] == L'0' && szToken[1] == L'x')
            {
                if (FAILED(hr = GetIntegerFromHexaString(szToken, value.liLargeInteger)))
                    return hr;
            }
            else
            {
                _set_errno(0L);
                value.liLargeInteger.QuadPart = _wtoi64(szToken);
                if ((value.liLargeInteger.QuadPart == 0LL) && (errno == ERANGE || errno == EINVAL))
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
                }
            }
            break;
        case DateTime:
            if (FAILED(hr = GetDateFromString(m_strDateFormat.c_str(), szToken, value.ftDateTime)))
                return hr;
            break;
        default:
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
    }

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::PeekTypes(DWORD cbLinesToPeek)
{
    HRESULT hr = E_FAIL;

    LARGE_INTEGER savedStoreBytes = m_liStoreBytes;
    LARGE_INTEGER savedPos = m_liCurPos;
    LARGE_INTEGER savedFilePos = m_liFilePos;
    ULONGLONG savedLineNum = m_ullCurLine;
    LPBYTE savedCursor = m_Store.Cursor();
    DWORD j = 0;

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock(CSV_READ_CHUNK_IN_BYTES * 2)))
        return hr;

    LPBYTE pStoreBackup = new BYTE[m_Store.SizeOfCursor()];
    if (pStoreBackup == nullptr)
        return E_OUTOFMEMORY;
    BOOST_SCOPE_EXIT((&pStoreBackup)) { delete[] pStoreBackup; }
    BOOST_SCOPE_EXIT_END

    size_t savedSize = m_Store.SizeOfCursor();
    CopyMemory(pStoreBackup, m_Store.Cursor(), m_Store.SizeOfCursor());

    for (j = 0; j < cbLinesToPeek; j++)
    {
        bool bReachedNewLine = false;
        bool bReachedEndOfFile = false;
        DWORD i = 0;

        for (i = 1; i < m_Schema.Column.size(); i++)
        {
            WCHAR* szToken = NULL;
            DWORD dwTokenLength = 0L;

            if (FAILED(hr = PeekNextToken(szToken, dwTokenLength, bReachedNewLine, bReachedEndOfFile)))
                return hr;

            ColumnType type = UnknownType;
            if (FAILED(hr = GuessType(szToken, dwTokenLength, type)))
                return hr;

            switch (type)
            {
                case String:
                    m_Schema.Column[i].Type = String;  // whatever it was, it is now a string
                    break;
                case Integer:
                    switch (m_Schema.Column[i].Type)
                    {
                        case String:  // left unchanged, we already had a string here
                            break;
                        case LargeInteger:  // left unchanged, we already had a bigger compatible integer here
                            break;
                        case DateTime:  // Incompatible types found; we turn to string
                            m_Schema.Column[i].Type = String;
                            break;
                        default:
                            m_Schema.Column[i].Type = Integer;
                            break;
                    }
                    break;
                case LargeInteger:
                    switch (m_Schema.Column[i].Type)
                    {
                        case String:  // left unchanged, we already had a string here
                            break;
                        case Integer:  // we need a bigger integer here
                            m_Schema.Column[i].Type = LargeInteger;
                            break;
                        case DateTime:  // Incompatible types found; we turn to string
                            m_Schema.Column[i].Type = String;
                            break;
                        default:
                            m_Schema.Column[i].Type = LargeInteger;
                            break;
                    }
                    break;
                case DateTime:
                    switch (m_Schema.Column[i].Type)
                    {
                        case String:  // left unchanged, we already had a string here. DateTime is not compatible
                            break;
                        case LargeInteger:
                        case Integer:  // Incompatible types found; we turn to string
                            m_Schema.Column[i].Type = String;
                            break;
                        default:
                            m_Schema.Column[i].Type = DateTime;
                            break;
                    }
            }

            m_Store.SetNewBottom((LPBYTE)(szToken + dwTokenLength));

            if (bReachedNewLine && i != m_Schema.Column.size() - 1)
            {
                Log::Error(
                    "Not enough columns found while peeking column types ({} found, {} expected)",
                    i,
                    m_Schema.Column.size() - 1);
                return E_FAIL;
            }
        }
        if (!bReachedNewLine)
        {
            Log::Error(
                "Too much columns found while peeking column types ({} found, {} expected)",
                i,
                m_Schema.Column.size() - 1);
            return E_FAIL;
        }
        else
            m_ullCurLine++;

        if (bReachedEndOfFile)
            break;  // EOF was reached before peeked is complete... nothing more can be done... break...
    }

    if (FAILED(hr = m_Store.Unlock()))
        return hr;

    m_Store.ResetCursor(savedCursor);
    if (savedSize > m_Store.SizeOfCursor())
        m_Store.ForwardEndOfCursorBy(savedSize - m_Store.SizeOfCursor());

    CopyMemory(m_Store.Cursor(), pStoreBackup, m_Store.SizeOfCursor());

    m_liCurPos = savedPos;
    m_liStoreBytes = savedStoreBytes;
    m_liFilePos = savedFilePos;
    m_ullCurLine = savedLineNum;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Cruncher::ParseNextLine(Record& record)
{
    HRESULT hr = E_FAIL;

    record.Source.ullLineNumber = m_ullCurLine;

    record.Values.resize(m_Schema.Column.size());

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock(CSV_READ_CHUNK_IN_BYTES)))
        return hr;

    LPBYTE szStartupPos = m_Store.Cursor();
    if (FAILED(hr = m_Store.SetNewBottom(szStartupPos)))
        return hr;
    if (FAILED(hr = m_Store.Unlock()))
        return hr;

    record.Values[0].Definition = &m_Schema.Column[0];

    bool bReachedNewLine = false;
    bool bReachedEndOfFile = false;
    DWORD i = 0;
    for (i = 1; i < m_Schema.Column.size(); i++)
    {
        WCHAR* szToken = NULL;
        DWORD dwTokenLength = 0L;
        LPBYTE szNextStartupPos = m_Store.Cursor();
        if (FAILED(hr = PeekNextToken(szToken, dwTokenLength, bReachedNewLine, bReachedEndOfFile)))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS))
            {
                // Let's organise a restart!
                m_liCurPos.QuadPart -= (LONGLONG)(m_Store.Cursor() - szNextStartupPos);
                return m_Store.ResetCursor(szNextStartupPos);
            }

            return hr;
        }

        record.Values[i].Definition = &m_Schema.Column[i];

        if (FAILED(hr = CoerceToColumn(szToken, dwTokenLength, record.Values[i])))
        {
            Log::Warn(L"Failed to coerce {} into its destination type [{}]", szToken, SystemError(hr));
        }
        if (bReachedNewLine && i != m_Schema.Column.size() - 1)
        {
            Log::Error(
                "Not enough columns found while peeking column types ({} found, {} expected)",
                i,
                m_Schema.Column.size() - 1);
            return E_FAIL;
        }
    }
    if (!bReachedNewLine)
    {
        Log::Error(
            "Too much columns while parsing line: {} ({} found, {} expected)",
            m_ullCurLine,
            i,
            m_Schema.Column.size() - 1);
        return E_FAIL;
    }
    else
        m_ullCurLine++;

    if (bReachedEndOfFile)
        return S_FALSE;

    return S_OK;
}

Orc::TableOutput::CSV::Cruncher::~Cruncher()
{
    m_liFilePos.QuadPart = 0;
    m_liStoreBytes.QuadPart = 0;
    m_liCurPos.QuadPart = 0;
    m_liFileSize.QuadPart = 0;
    m_ullCurLine = 0;
}
