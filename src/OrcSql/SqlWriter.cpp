//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "SqlWriter.h"
#include "SqlConnection.h"

#include "ConfigFile.h"
#include "LogFileWriter.h"
#include "ParameterCheck.h"
#include "WideAnsi.h"

#include <sqltypes.h>
#include <sql.h>
#include <sqlext.h>
#include <sqlncli.h>

#include <boost\scope_exit.hpp>

using namespace Orc;
using namespace Orc::TableOutput;

Orc::TableOutput::Sql::Writer::Writer(logger pLog, DWORD dwTransactionRowCount)
    : _L_(std::move(pLog))
    , m_dwMaxTransactionRowCount(dwTransactionRowCount)
    , m_dwRowsInTransaction(0)
    , m_dwSentCount(0)
    , m_dwWroteCount(0)
    , m_CurCol(1)
    , m_bBound(false)
{
}

HRESULT Orc::TableOutput::Sql::Writer::SetConnection(const std::shared_ptr<IConnection>& pConnection)
{

    if (!pConnection->IsConnected())
    {
        log::Error(_L_, E_INVALIDARG, L"Provided SQL connection is not connected\r\n");
        return E_INVALIDARG;
    }

    auto connection = std::dynamic_pointer_cast<Connection>(pConnection);
    if (!connection)
    {
        log::Error(_L_, E_INVALIDARG, L"Invalid SQL connection\r\n");
        return E_INVALIDARG;
    }
    m_pSQL = connection;
    return S_OK;
};

HRESULT Orc::TableOutput::Sql::Writer::AddColumn(
    DWORD dwColId,
    const std::wstring& strName,
    ColumnType type,
    std::optional<DWORD> dwMaxLen,
    std::optional<DWORD> dwLen)
{

    if (m_Columns.empty())  // We need a "dummy" Col0
    {
        BoundColumn aCol;
        aCol.ColumnName = L"NothingInCol0";
        aCol.Type = ColumnType::Nothing;
        aCol.dwLen = 0;
        aCol.dwMaxLen = 0;
        aCol.dwColumnID = 0;

        m_Columns.push_back(aCol);
    }

    BoundColumn aCol;
    aCol.ColumnName = strName;
    aCol.Type = type;
    aCol.dwLen = dwLen;
    aCol.dwMaxLen = dwMaxLen;
    aCol.dwColumnID = dwColId;

    m_Columns.emplace_back(std::move(aCol));

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::SetSchema(const TableOutput::Schema& columns)
{
    DWORD dwColID = 1;

    if (!columns)
        return E_POINTER;

    for (const auto& aCol : columns)
    {
        if (aCol->dwColumnID > 0)
            AddColumn(dwColID++, aCol->ColumnName, aCol->Type, aCol->dwMaxLen, aCol->dwLen);
    }

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::ClearTable(LPCWSTR)
{
    // std::string strCmd = "truncate table dbo.";
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::BindColumns(LPCWSTR szTable)
{
    using namespace msl::utilities;

    if (m_pSQL == nullptr)
        return E_FAIL;

    HRESULT hr = S_OK;
    SQLRETURN retcode = SQL_ERROR;

    // Initialize bcp.
    if ((retcode = m_pSQL->bcp_init(szTable, NULL, NULL, DB_IN)) == FAIL)
    {
        // Raise error and return.
        m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, retcode);
        m_pSQL->Disconnect();
        return E_FAIL;
    }
    log::Verbose(_L_, L"VERBOSE: Successfully initialised bulk copy to table %s\r\n", szTable);

    static NoData NoBoundData = { SQL_NULL_DATA };

    m_Columns.shrink_to_fit();
    if (m_Columns.empty())
        return S_OK;

    std::for_each(++begin(m_Columns), end(m_Columns), [this, &hr](BoundColumn& item) {
        hr = S_OK;
        switch (item.Type)
        {
        case ColumnType::Nothing:
            if (m_pSQL->bcp_bind((LPBYTE)&NoBoundData, 0, SQL_NULL_DATA, NULL, 0, 0, item.dwColumnID) == FAIL)
            {
                m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                hr = E_FAIL;
            }
            break;
        case ColumnType::UInt64Type:
            ZeroMemory(&(item.boundData.LargeInt), sizeof(item.boundData.LargeInt));
            if (m_pSQL->bcp_bind(
                (LPCBYTE)&item.boundData.LargeInt,
                0,
                sizeof(item.boundData.LargeInt),
                NULL,
                0,
                SQLINT8,
                item.dwColumnID)
                == FAIL)
            {
                m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                hr = E_FAIL;
            }
            break;
        case ColumnType::BoolType:
            ZeroMemory(&(item.boundData.Boolean), sizeof(item.boundData.Boolean));
            if (m_pSQL->bcp_bind((LPBYTE) & (item.boundData.Boolean), 0, 0, NULL, 0, SQLBIT, item.dwColumnID)
                == FAIL)
            {
                m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                hr = E_FAIL;
            }
            break;
        case ColumnType::UInt32Type:
            ZeroMemory(&(item.boundData.Dword), sizeof(item.boundData.Dword));
            if (m_pSQL->bcp_bind(
                (LPBYTE) & (item.boundData.Dword),
                0,
                sizeof(item.boundData.Dword),
                NULL,
                0,
                SQLINT4,
                item.dwColumnID)
                == FAIL)
            {
                m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                hr = E_FAIL;
            }
            break;
        case ColumnType::TimeStampType:
            ZeroMemory((LPBYTE) & (item.boundData.TimeStamp), sizeof(item.boundData.TimeStamp));
            item.boundData.TimeStamp.year = 1;
            item.boundData.TimeStamp.month = 1;
            item.boundData.TimeStamp.day = 1;
            if (m_pSQL->bcp_bind(
                (LPBYTE) & (item.boundData.TimeStamp),
                0,
                sizeof(item.boundData.TimeStamp),
                NULL,
                0,
                SQLDATETIME2N,
                item.dwColumnID)
                == FAIL)
            {
                m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                hr = E_FAIL;
            }
            break;
        case ColumnType::UTF16Type:
            if (item.dwMaxLen.has_value())
            {
                item.boundData.WString =
                    (WStringData*)malloc(SafeInt<DWORD>(item.dwMaxLen.value()) * sizeof(WCHAR) + sizeof(size_t));
                if (item.boundData.WString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.WString, sizeof(size_t) + item.dwMaxLen.value() * sizeof(WCHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.WString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLNVARCHAR,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else if (item.dwLen.has_value())
            {
                item.boundData.WString =
                    (WStringData*)malloc(SafeInt<DWORD>(item.dwLen.value()) * sizeof(WCHAR) + sizeof(size_t));
                if (item.boundData.WString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.WString, sizeof(size_t) + item.dwLen.value() * sizeof(WCHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.WString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLNCHAR,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else
            {
                item.boundData.WString =
                    (WStringData*)malloc(SafeInt<DWORD>(DBMAXCHAR) * sizeof(WCHAR) + sizeof(size_t));
                if (item.boundData.WString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.WString, sizeof(size_t) + DBMAXCHAR * sizeof(WCHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.WString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLNCHAR,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            break;
        case ColumnType::UTF8Type:
            if (item.dwMaxLen.has_value())
            {
                item.boundData.AString =
                    (AStringData*)malloc(SafeInt<DWORD>(sizeof(size_t)) + item.dwMaxLen.value() * sizeof(CHAR));
                if (item.boundData.AString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.AString, sizeof(size_t) + item.dwMaxLen.value() * sizeof(CHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.AString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLVARCHAR,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else if (item.dwLen.has_value())
            {
                item.boundData.AString =
                    (AStringData*)malloc(SafeInt<DWORD>(sizeof(size_t)) + item.dwLen.value() * sizeof(CHAR));
                if (item.boundData.AString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.AString, sizeof(size_t) + item.dwLen.value() * sizeof(CHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.AString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLCHARACTER,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else
            {
                item.boundData.AString =
                    (AStringData*)malloc(SafeInt<DWORD>(sizeof(size_t)) + DBMAXCHAR * sizeof(CHAR));
                if (item.boundData.AString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.AString, sizeof(size_t) + DBMAXCHAR * sizeof(CHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.AString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLCHARACTER,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            break;
        case ColumnType::BinaryType:
            if (item.dwMaxLen.has_value())
            {
                item.boundData.Binary = (BinaryData*)malloc(SafeInt<DWORD>(item.dwMaxLen.value()) + sizeof(size_t));
                if (item.boundData.Binary == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.Binary, item.dwMaxLen.value() + sizeof(size_t));
                if (m_pSQL->bcp_bind(
                    (LPCBYTE)item.boundData.Binary,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    nullptr,
                    0L,
                    SQLVARBINARY,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else if (item.dwLen.has_value())
            {
                item.boundData.Binary = (BinaryData*)malloc(SafeInt<DWORD>(item.dwLen.value()) + sizeof(size_t));
                if (item.boundData.Binary == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.Binary, item.dwLen.value() + sizeof(size_t));
                if (m_pSQL->bcp_bind(
                    (LPCBYTE)item.boundData.Binary,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    nullptr,
                    0L,
                    SQLBINARY,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else
            {
                item.boundData.Binary = (BinaryData*)malloc(SafeInt<DWORD>(DBMAXCHAR) + sizeof(size_t));
                if (item.boundData.Binary == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.Binary, DBMAXCHAR + sizeof(size_t));
                if (m_pSQL->bcp_bind(
                    (LPCBYTE)item.boundData.Binary,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    nullptr,
                    0L,
                    SQLBINARY,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            break;
        case ColumnType::GUIDType:
            ZeroMemory(&(item.boundData.GUID), sizeof(item.boundData.GUID));
            if (m_pSQL->bcp_bind(
                (LPCBYTE)&item.boundData.GUID,
                0,
                sizeof(item.boundData.GUID),
                NULL,
                0,
                SQLBINARY,
                item.dwColumnID)
                == FAIL)
            {
                m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                hr = E_FAIL;
            }
            break;
        case ColumnType::XMLType:
            if (item.dwMaxLen.has_value())
            {
                msl::utilities::SafeInt<DWORD> buffer_size = sizeof(WCHAR);

                buffer_size *= item.dwMaxLen.value();
                buffer_size += sizeof(size_t);

                item.boundData.WString = (WStringData*)malloc(buffer_size);
                if (item.boundData.WString == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                ZeroMemory(item.boundData.WString, sizeof(size_t) + item.dwMaxLen.value() * sizeof(WCHAR));
                if (m_pSQL->bcp_bind(
                    (LPBYTE)item.boundData.WString,
                    sizeof(size_t),
                    SQL_VARLEN_DATA,
                    NULL,
                    0,
                    SQLXML,
                    item.dwColumnID)
                    == FAIL)
                {
                    m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, FAIL);
                    hr = E_FAIL;
                }
            }
            else
                hr = E_INVALIDARG;
            break;
        default:
            break;
        }
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to bind column %s (ID=%d)\r\n", item.ColumnName.c_str(), item.dwColumnID);
            return;
        }
        else
        {
            log::Verbose(_L_, L"INFO: Column %s (id=%d) is now bound\r\n", item.ColumnName.c_str(), item.dwColumnID);
        }
        return;
    });
    if (FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }

    m_bBound = true;
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::ClearBoundData()
{
    std::for_each(begin(m_Columns), end(m_Columns), [this](TableOutput::BoundColumn& definition) {
        definition.ClearBoundData();
    });
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::SendRow()
{
    if (!m_pSQL->IsConnected())
        return E_FAIL;

    HRESULT hr = E_FAIL;

    if (!m_bBound)
    {
        log::Error(_L_, E_NOT_SET, L"Attempt to send row without bound columns\r\n");
        return E_NOT_SET;
    }

    if (m_pSQL->bcp_sendrow() == FAIL)
    {
        m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, -1);
        m_CurCol = 1;
        return E_FAIL;
    }
    m_CurCol = 1;
    if (FAILED(hr = ClearBoundData()))
        return hr;

    m_dwWroteCount++;
    m_dwRowsInTransaction++;
    if (m_dwRowsInTransaction >= m_dwMaxTransactionRowCount)
    {
        if (FAILED(hr = Flush()))
            return hr;
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::AbandonRow()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = ClearBoundData()))
        return hr;
    m_CurCol = 1;
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::AbandonColumn()
{
    m_Columns[m_CurCol].ClearBoundData();
    m_CurCol++;
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::Done()
{
    if (!m_pSQL->IsConnected())
        return E_FAIL;

    DBINT nbRows = 0;
    if ((nbRows = m_pSQL->bcp_done()) == -1)
    {
        m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, -1);
        return E_FAIL;
    }
    m_dwSentCount += nbRows;
    log::Verbose(_L_, L"INFO: Wrote %d accumulated rows\r\n", m_dwWroteCount);
    log::Verbose(_L_, L"INFO: Send %d accumulated rows\r\n", m_dwSentCount);
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::Flush()
{
    DBINT nbSentRows = 0L;
    if ((nbSentRows = m_pSQL->bcp_batch()) == -1)
    {
        m_pSQL->HandleDiagnosticRecord(SQL_HANDLE_DBC, -1);
        return E_FAIL;
    }
    m_dwSentCount += nbSentRows;
    log::Verbose(_L_, L"VERBOSE: Successfully comitted %d rows\r\n", m_dwRowsInTransaction);
    m_dwRowsInTransaction = 0;
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::Close()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = Done()))
    {
        log::Error(_L_, hr, L"Failed to bcp_done the last batch of data\r\n");
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteCharArray(const CHAR* szString, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteCharArray(szString, dwCharCount)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteString(const WCHAR* szString)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteString(szString)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteString(const CHAR* szString)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteString(szString)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteFormated_(const std::string_view& szFormat, IOutput::format_args args)
{
    return E_NOTIMPL;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteCharArray(const WCHAR* szString, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteCharArray(szString, dwCharCount)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP
Orc::TableOutput::Sql::Writer::WriteFormated_(const std::wstring_view& szFormat, IOutput::wformat_args args)
{
    return E_NOTIMPL;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteAttributes(DWORD dwAttributes)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteAttributes(dwAttributes)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteFileTime(FILETIME fileTime)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteFileTime(fileTime)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteFileTime(LONGLONG fileTime)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteFileTime(fileTime)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteFileSize(LARGE_INTEGER fileSize)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteFileSize(fileSize)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteFileSize(ULONGLONG fileSize)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteFileSize(fileSize)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteFileSize(nFileSizeHigh, nFileSizeLow)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteInteger(DWORD dwInteger)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteInteger(dwInteger)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteInteger(LONGLONG dw64Integer)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteInteger(dw64Integer)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteInteger(ULONGLONG dw64Integer)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteInteger(dw64Integer)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteBytes(const BYTE pBytes[], DWORD dwLen)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteBytesInHex(pBytes, dwLen)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteBytes(const CBinaryBuffer& Buffer)
{
    return WriteBytes(Buffer.GetData(), (DWORD)Buffer.GetCount());
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteBool(bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteBool(bBoolean)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[])
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteEnum(dwEnum, EnumValues)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP
Orc::TableOutput::Sql::Writer::WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteFlags(dwFlags, FlagValues, cSeparator)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteExactFlags(dwFlags, FlagValues)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteGUID(const GUID& guid)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteGUID(guid)))
        AbandonColumn();
    else
        m_CurCol++;

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteXML(const CHAR* szString)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteString(szString)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteXML(const WCHAR* szXML)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteString(szXML)))
        AbandonColumn();
    else
        m_CurCol++;

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteXML(const WCHAR* szString, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteCharArray(szString, dwCharCount)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

STDMETHODIMP Orc::TableOutput::Sql::Writer::WriteXML(const CHAR* szString, DWORD dwCharCount)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_Columns[m_CurCol].WriteCharArray(szString, dwCharCount)))
        AbandonColumn();
    else
        m_CurCol++;
    return hr;
}

HRESULT Orc::TableOutput::Sql::Writer::WriteEndOfLine()
{
    if (m_CurCol != m_Columns.size())
    {
        log::Error(
            _L_,
            E_UNEXPECTED,
            L"End of row does not have appropriate writes (columns=%d, writes=%d)\r\n",
            m_Columns.size(),
            m_CurCol);
        AbandonRow();
        return E_UNEXPECTED;
    }
    return SendRow();
}

Orc::TableOutput::Sql::Writer::~Writer()
{
    std::for_each(std::begin(m_Columns), std::end(m_Columns), [](BoundColumn& coldef) {
        if (coldef.Type == ColumnType::UTF16Type)
        {
            free(coldef.boundData.WString);
            coldef.boundData.WString = nullptr;
        }
        if (coldef.Type == ColumnType::BinaryType)
        {
            free(coldef.boundData.Binary);
            coldef.boundData.Binary = nullptr;
        }
    });

    _L_.reset();
}
