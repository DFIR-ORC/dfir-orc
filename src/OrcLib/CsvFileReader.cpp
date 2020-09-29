//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"
#include "CSVFileReader.h"

#include "ParameterCheck.h"

#include "TableOutput.h"

#include "FileStream.h"
#include "Convert.h"

#include "boost\scope_exit.hpp"

using namespace std;

using namespace Orc;

static const auto CSV_READ_CHUNK_IN_BYTES = 40960;

Orc::TableOutput::CSV::FileReader::FileReader()
{
    m_wcSeparator = L',';
    m_wcQuote = L'\"';
    m_dwSkipLines = 0L;
    m_bfirstRowIsColumnNames = true;

    m_liFilePos.QuadPart = 0;
    m_liFileSize.QuadPart = 0;
    m_liStoreBytes.QuadPart = 0;
    m_liCurPos.QuadPart = 0;
    m_ullCurLine = 1;

    m_pUTF8Buffer = NULL;
    m_csvEncoding = OutputSpec::Encoding::kUnknown;
}

HRESULT Orc::TableOutput::CSV::FileReader::SetDateFormat(const WCHAR* szDateFormat)
{
    if (m_liCurPos.QuadPart > 0)
        return RPC_E_TOO_LATE;
    m_strDateFormat.clear();
    m_strDateFormat.assign(szDateFormat);

    if (m_strDateFormat.empty())
        return E_INVALIDARG;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::SetBooleanFormat(const WCHAR* szBooleanFormat)
{
    if (wcslen(szBooleanFormat) < 2)
        return E_INVALIDARG;

    m_BoolChar[0] = szBooleanFormat[0];
    m_BoolChar[1] = szBooleanFormat[1];
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::OpenStream(
    const std::shared_ptr<ByteStream>& pStream,
    bool bfirstRowIsColumnNames,
    WCHAR wcSeparator,
    WCHAR wcQuote,
    const WCHAR* szDateFormat,
    const WCHAR* szBoolFormat,
    DWORD dwSkipLines)
{
    HRESULT hr = E_FAIL;

    m_wcSeparator = wcSeparator;
    m_wcQuote = wcQuote;
    m_strDateFormat.assign(szDateFormat);
    m_dwSkipLines = dwSkipLines;
    m_bfirstRowIsColumnNames = bfirstRowIsColumnNames;

    m_pStream = pStream;

    if (FAILED(hr = m_Store.InitializeStorage(CSV_READ_CHUNK_IN_BYTES * 20, CSV_READ_CHUNK_IN_BYTES * 2)))
        return hr;

    m_liFileSize.QuadPart = m_pStream->GetSize();

    m_liFilePos.QuadPart = 0;
    m_liFileSize.QuadPart = 0;
    m_liCurPos.QuadPart = 0;
    m_ullCurLine = 1;

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock(CSV_READ_CHUNK_IN_BYTES * 2)))
        return hr;
    if (FAILED(hr = m_Store.Unlock()))
        return hr;

    BYTE BOMBuffer[4];
    ULONGLONG nbRead = 0L;

    if (FAILED(hr = m_pStream->Read(BOMBuffer, 4, &nbRead)))
        return hr;

    // skipping BOM
    BYTE* Current = BOMBuffer;
    BYTE utf16bom[2] = {0xFF, 0xFE};
    if (Current[0] == utf16bom[0] && Current[1] == utf16bom[1])
    {
        m_csvEncoding = OutputSpec::Encoding::UTF16;
        m_pStream->SetFilePointer(2L, 0L, FILE_BEGIN);
        m_liCurPos.QuadPart = 2LL;
        m_liFilePos.QuadPart = 2LL;
        spdlog::debug(L"UTF16 BOM detected");
    }
    BYTE utf8bom[3] = {0xEF, 0xBB, 0xBF};
    if (Current[0] == utf8bom[0] && Current[1] == utf8bom[1] && Current[2] == utf8bom[2])
    {
        m_csvEncoding = OutputSpec::Encoding::UTF8;
        spdlog::debug(L"UTF8 BOM detected");
        m_pStream->SetFilePointer(3L, 0L, FILE_BEGIN);
        m_liCurPos.QuadPart = 3LL;
        m_liFilePos.QuadPart = 3LL;
        m_pUTF8Buffer = (LPBYTE)VirtualAlloc(NULL, CSV_READ_CHUNK_IN_BYTES / 2, MEM_COMMIT, PAGE_READWRITE);
        if (m_pUTF8Buffer == NULL)
            return E_OUTOFMEMORY;
    }

    if (FAILED(hr = SetBooleanFormat(szBoolFormat)))
    {
        spdlog::error(L"Failed to set boolean format from '{}' (code: {:#x})", szBoolFormat, hr);
    }

    if (m_csvEncoding == OutputSpec::Encoding::kUnknown)
    {
        spdlog::debug(L"No valid BOM detected, defaulting to UTF8");
        m_csvEncoding = OutputSpec::Encoding::UTF8;
    }

    if (FAILED(hr = ReadMoreData()))
        return hr;

    if (FAILED(hr = SkipLines()))
        return hr;

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::OpenFile(
    const WCHAR* szFileName,
    bool bfirstRowIsColumnNames,
    WCHAR wcSeparator,
    WCHAR wcQuote,
    const WCHAR* szDateFormat,
    const WCHAR* szBoolFormat,
    DWORD dwSkipLines)
{
    HRESULT hr = E_FAIL;

    std::shared_ptr<FileStream> fileStream = std::make_shared<FileStream>();

    if (FAILED(
            hr = fileStream->OpenFile(
                szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL)))
        return hr;

    if (FAILED(
            hr = OpenStream(
                fileStream, bfirstRowIsColumnNames, wcSeparator, wcQuote, szDateFormat, szBoolFormat, dwSkipLines)))
        return hr;

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::ReadMoreData()
{
    HRESULT hr = E_FAIL;

    ULONGLONG nbRead = 0ULL;
    ULONGLONG nbBytesAdded = 0ULL;

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock(CSV_READ_CHUNK_IN_BYTES)))
        return hr;

    switch (m_csvEncoding)
    {
        case OutputSpec::Encoding::UTF16:
        {
            if (FAILED(hr = m_pStream->Read(m_Store.EndOfCursor(), CSV_READ_CHUNK_IN_BYTES, &nbRead)))
            {
                if (hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
                    return hr;
            }
            else if (nbRead == 0ULL)
            {
                // Why did readfile NOT return ERROR_HANDLE_EOF???
                return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
            }

            nbBytesAdded = nbRead;
            m_liFilePos.QuadPart += nbRead;
            m_liStoreBytes.QuadPart += nbRead;
        }
        break;
        case OutputSpec::Encoding::UTF8:
        {
            if (FAILED(hr = m_pStream->Read(m_pUTF8Buffer, CSV_READ_CHUNK_IN_BYTES / 2, &nbRead)))
            {
                if (hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
                    return hr;
            }
            else if (nbRead == 0L)
            {
                // Why did readfile NOT return ERROR_HANDLE_EOF???
                return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
            }

            m_liFilePos.QuadPart += nbRead;

            if (nbRead > MAXDWORD)
                return E_INVALIDARG;

            nbBytesAdded = MultiByteToWideChar(
                CP_UTF8,
                0L,
                (LPCSTR)m_pUTF8Buffer,
                (DWORD)nbRead,
                (LPWSTR)m_Store.EndOfCursor(),
                CSV_READ_CHUNK_IN_BYTES);
            if (nbBytesAdded == 0)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            nbBytesAdded *= sizeof(WCHAR);
            m_liStoreBytes.QuadPart += nbBytesAdded;
        }
        break;
    }

    m_Store.Unlock();

    HRESULT hr2 = E_FAIL;
    if (FAILED(hr2 = m_Store.ForwardEndOfCursorBy((DWORD)nbBytesAdded)))
        return hr2;

    if (hr == HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
        return hr;

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::ResetCurPos(WCHAR* pNewCurPos)
{
    HRESULT hr = E_FAIL;
    WCHAR* pOldPos = (WCHAR*)m_Store.Cursor();

    if (FAILED(hr = m_Store.ResetCursor((LPBYTE)pNewCurPos)))
        return hr;

    DWORDLONG dwPosChange = (LPBYTE)pOldPos - (LPBYTE)pNewCurPos;
    m_liCurPos.QuadPart -= dwPosChange;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::PeekNextToken(
    WCHAR*& szToken,
    DWORD& dwTokenLengthInChars,
    bool& bReachedNewLine,
    bool& bReachedEndOfFile)
{
    if (m_liCurPos.QuadPart >= m_liStoreBytes.QuadPart)
        return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);

    bReachedNewLine = false;
    bReachedEndOfFile = false;

    bool inQuotes = false;

    if (m_Store.Cursor() + 3 * sizeof(WCHAR) >= m_Store.EndOfCursor())
    {
        return HRESULT_FROM_WIN32(ERROR_SUCCESS_RESTART_REQUIRED);
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
                            spdlog::warn("Invalid line termination");
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
                        spdlog::warn("Invalid line termination");
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
            return HRESULT_FROM_WIN32(ERROR_SUCCESS_RESTART_REQUIRED);
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

HRESULT Orc::TableOutput::CSV::FileReader::SkipLines()
{
    HRESULT hr = E_FAIL;
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

            if (FAILED(hr = ReadMoreData()))
            {
                if (hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
                    return hr;
            }
        }
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::PeekHeaders(const WCHAR* szHeadersFileName)
{
    HRESULT hr = E_FAIL;

    if (!m_bfirstRowIsColumnNames && szHeadersFileName)
    {
        spdlog::error("No headers in headers file nor in first line. Unsupported");
        return E_INVALIDARG;
    }

    FileReader* pHeaderReader = this;

    if (szHeadersFileName != NULL)
    {
        pHeaderReader = new FileReader();

        if (pHeaderReader == NULL)
            return E_OUTOFMEMORY;

        pHeaderReader->OpenFile(szHeadersFileName);
    }

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
            if (aCol.Name.back() == L'\"')
            {
                aCol.Name.resize(aCol.Name.size() - 1);
            }
        }
        m_Schema.Column.push_back(std::move(aCol));

        if (bReachedNewLine)
        {
            m_ullCurLine++;
            break;
        }
    }

    if (szHeadersFileName != NULL)
    {
        delete pHeaderReader;
        pHeaderReader = NULL;
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::GuessType(WCHAR* szToken, DWORD dwTokenLength, ColumnType& type)
{
    HRESULT hr = E_FAIL;

    if (dwTokenLength == 0)
    {
        type = UnknownType;
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
        if (SUCCEEDED(hr = GetDateFromString(m_strDateFormat.c_str(), szToken, result)))
        {
            type = DateTime;
            return S_OK;
        }
    }
    {
        if (dwTokenLength == 1)
        {
            if (szToken[0] == m_BoolChar[0] || szToken[0] == m_BoolChar[1])
            {
                type = Boolean;
                return S_OK;
            }
        }
    }
    {
        CLSID clsid;
        if (SUCCEEDED(hr = CLSIDFromString(szToken, &clsid)))
        {
            type = GUIDType;
            return S_OK;
        }
    }

    type = String;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::CoerceToColumn(WCHAR* szToken, DWORD dwTokenLength, Column& value)
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
                try
                {
                    value.dwInteger = ConvertTo<DWORD>(_wtoi(szToken));
                }
                catch (const std::overflow_error&)
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
                }
                if ((value.dwInteger == 0L) && (errno == ERANGE || errno == EINVAL))
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
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
            if (dwTokenLength)
            {
                if (FAILED(hr = GetDateFromString(m_strDateFormat.c_str(), szToken, value.ftDateTime)))
                    return hr;
            }
            else
            {
                ZeroMemory(&value.ftDateTime, sizeof(value.ftDateTime));
            }
            break;
        case GUIDType:
            if (FAILED(hr = CLSIDFromString(szToken, &value.guid)))
                return hr;
            break;
        case Boolean:
        {
            if (dwTokenLength == 1)
            {
                if (*szToken == m_BoolChar[0])
                {
                    value.boolean = true;
                    return S_OK;
                }
                else if (*szToken == m_BoolChar[1])
                {
                    value.boolean = false;
                    return S_OK;
                }
                else
                {
                    return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
                }
            }
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
        }
        default:
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATATYPE);
    }

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::PeekTypes(DWORD cbLinesToPeek)
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

        for (auto& col : m_Schema.Column)
        {
            if (col.Index == 0)
                continue;  // skip firt elt, unused

            WCHAR* szToken = NULL;
            DWORD dwTokenLength = 0L;

            if (FAILED(hr = PeekNextToken(szToken, dwTokenLength, bReachedNewLine, bReachedEndOfFile)))
                return hr;

            ColumnType type = UnknownType;
            if (FAILED(hr = GuessType(szToken, dwTokenLength, type)))
                return hr;

            ColumnType new_type = UnknownType;

            switch (type)
            {
                case String:
                    new_type = String;  // whatever it was, it is now a string
                    break;
                case Integer:
                    switch (col.Type)
                    {
                        case String:  // left unchanged, we already had a string here
                            new_type = String;
                            break;
                        case LargeInteger:  // left unchanged, we already had a bigger compatible integer here
                            break;
                        case DateTime:  // Incompatible types found; we turn to string
                            new_type = String;
                            break;
                        default:
                            new_type = Integer;
                            break;
                    }
                    break;
                case LargeInteger:
                    switch (col.Type)
                    {
                        case String:  // left unchanged, we already had a string here
                            new_type = String;
                            break;
                        case Integer:  // we need a bigger integer here
                            new_type = LargeInteger;
                            break;
                        case DateTime:  // Incompatible types found; we turn to string
                            new_type = String;
                            break;
                        default:
                            new_type = LargeInteger;
                            break;
                    }
                    break;
                case DateTime:
                    switch (col.Type)
                    {
                        case String:  // left unchanged, we already had a string here. DateTime is not compatible
                            new_type = String;
                            break;
                        case LargeInteger:
                        case Integer:  // Incompatible types found; we turn to string
                            new_type = String;
                            break;
                        default:
                            new_type = DateTime;
                            break;
                    }
                    break;
                case GUIDType:
                    new_type = GUIDType;
                    break;
            }

            if (col.Type != UnknownType && new_type != col.Type && new_type != UnknownType)
            {
                spdlog::warn(
                    L"Column {}: Type of '{}' is incoherent with previous values -> converting to string",
                    col.Name,
                    szToken);
                col.Type = String;
            }
            else if (col.Type == UnknownType)
            {
                col.Type = new_type;
            }

            m_Store.SetNewBottom((LPBYTE)(szToken + dwTokenLength));

            if (bReachedNewLine && col.Index != m_Schema.Column.size() - 1)
            {
                spdlog::error(
                    "Not enough columns found while peeking column types ({} found, {} expected)",
                    col.Index,
                    m_Schema.Column.size() - 1);
                return E_FAIL;
            }
        }
        if (!bReachedNewLine)
        {
            spdlog::error(
                "Too much columns found while peeking column types ({} expected)", m_Schema.Column.size() - 1);
            return E_FAIL;
        }
        else
            m_ullCurLine++;

        if (bReachedEndOfFile)
            break;  // EOF was reached before peeked is complete... nothing more can be done... break...
    }

    if (FAILED(hr = m_Store.Unlock()))
        return hr;

    // restore position to before "peeking" types
    m_pStream->SetFilePointer(savedFilePos.QuadPart, FILE_BEGIN, (PULONG64)&savedFilePos.QuadPart);

    m_Store.ResetCursor(savedCursor);
    if (savedSize > m_Store.SizeOfCursor())
        m_Store.ForwardEndOfCursorBy(savedSize - m_Store.SizeOfCursor());

    CopyMemory(m_Store.Cursor(), pStoreBackup, m_Store.SizeOfCursor());

    m_liCurPos = savedPos;
    m_liStoreBytes = savedStoreBytes;
    m_liFilePos = savedFilePos;
    m_ullCurLine = savedLineNum;

    for (auto& col : m_Schema.Column)
    {
        if (col.Type == UnknownType)
            col.Type = String;
    }

    if (FAILED(hr = ReadMoreData()))
    {
        if (hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
            return hr;  // it is not an issue if peek types hit EOF
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::SetSchema(const TableOutput::Schema& columns)
{
    if (!columns)
        return E_POINTER;

    for (const auto& col : columns)
    {
        ColumnDef aCol;

        aCol.Index = col->dwColumnID;
        aCol.Name = col->ColumnName;
        switch (col->Type)
        {
            case TableOutput::ColumnType::Nothing:
                aCol.Type = UnknownType;
                break;
            case TableOutput::ColumnType::BoolType:
                aCol.Type = Boolean;
                break;
            case TableOutput::ColumnType::UInt32Type:
                aCol.Type = Integer;
                break;
            case TableOutput::ColumnType::UInt64Type:
                aCol.Type = LargeInteger;
                break;
            case TableOutput::ColumnType::TimeStampType:
                aCol.Type = DateTime;
                break;
            case TableOutput::ColumnType::UTF16Type:
            case TableOutput::ColumnType::UTF8Type:
                aCol.Type = String;
                break;
            case TableOutput::ColumnType::BinaryType:
                aCol.Type = String;
                break;
            case TableOutput::ColumnType::GUIDType:
                aCol.Type = GUIDType;
                break;
            default:
                aCol.Type = UnknownType;
                break;
        }
        m_Schema.Column.push_back(std::move(aCol));
    }

    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::SkipHeaders()
{
    if (!m_bfirstRowIsColumnNames)
        return S_OK;

    HRESULT hr = S_OK;
    WCHAR* szToken = NULL;
    DWORD dwTokenSize = 0L;
    bool bReachedNewLine = false;
    bool bReachedEndOfFile = false;

    while (SUCCEEDED(hr = PeekNextToken(szToken, dwTokenSize, bReachedNewLine, bReachedEndOfFile)))
    {
        if (bReachedNewLine)
        {
            break;
        }
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::FileReader::ParseNextLine(Record& record)
{
    HRESULT hr = E_FAIL;

    record.ullLineNumber = m_ullCurLine;

    record.Values.resize(m_Schema.Column.size());

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock(CSV_READ_CHUNK_IN_BYTES)))
        return hr;

    if (m_Store.SizeOfCursor() < CSV_READ_CHUNK_IN_BYTES / 4)
        if (FAILED(hr = ReadMoreData()))
        {
            if (hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
                return hr;
        }

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
        LPBYTE szStartupPos = m_Store.Cursor();
        if (FAILED(hr = PeekNextToken(szToken, dwTokenLength, bReachedNewLine, bReachedEndOfFile)))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_SUCCESS_RESTART_REQUIRED))
            {
                // Let's organise a restart!
                m_liCurPos.QuadPart -= (LONGLONG)(m_Store.Cursor() - szStartupPos);
                if (FAILED(hr = m_Store.ResetCursor(szStartupPos)))
                    return hr;
                if (FAILED(hr = ReadMoreData()))
                    return hr;

                if (FAILED(hr = PeekNextToken(szToken, dwTokenLength, bReachedNewLine, bReachedEndOfFile)))
                    return hr;
            }
            else
                return hr;
        }

        record.Values[i].Definition = &m_Schema.Column[i];

        if (FAILED(hr = CoerceToColumn(szToken, dwTokenLength, record.Values[i])))
        {
            spdlog::warn(L"Failed to coerce {} into its destination type (code: {:#x})", szToken, hr);
        }
        if (bReachedNewLine && i != m_Schema.Column.size() - 1)
        {
            spdlog::error(
                L"Not enough columns found while peeking column types ({} found, {} expected)",
                i,
                m_Schema.Column.size() - 1);
            return E_FAIL;
        }
    }
    if (!bReachedNewLine)
    {
        spdlog::error(
            "Too much columns while parsing line {} ({} found, {} expected)",
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

Orc::TableOutput::CSV::FileReader::~FileReader(void)
{
    m_pStream = nullptr;
    m_liFilePos.QuadPart = 0;
    m_liStoreBytes.QuadPart = 0;
    m_liCurPos.QuadPart = 0;
    m_liFileSize.QuadPart = 0;
    m_ullCurLine = 0;
}
