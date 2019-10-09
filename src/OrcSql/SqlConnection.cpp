//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CaseInsensitive.h"

#include "SQLConnection.h"

#include "LogFileWriter.h"

#include <sqltypes.h>
#include <sql.h>
#include <sqlext.h>
#include <sstream>

#include <boost\scope_exit.hpp>

using namespace std;

using namespace Orc;
using namespace Orc::TableOutput::Sql;

STDMETHODIMP Connection::Initialize()
{
    HRESULT hr = E_FAIL;
    if (!bSqlNativeInitialized)
    {
        if (FAILED(hr = LoadSqlNativeClient()))
        {
            log::Error(_L_, hr, L"Failed to load SQL Native Client (path=%s)\r\n", L"SQLNCLI11.DLL");
            return hr;
        }
        bSqlNativeInitialized = true;
    }
    return S_OK;
}

STDMETHODIMP Connection::Connect(const std::wstring& strConnString)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = Initialize()))
    {
        log::Error(_L_, hr, L"Failed to initialize SQL connection\r\n");
        return hr;
    }

    SQLRETURN retcode = SQL_ERROR;

    // Allocate environment handle
    retcode = pSqlAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_henv);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleDiagnosticRecord(SQL_HANDLE_ENV, retcode);
        return E_FAIL;
    }

    // Set the ODBC version environment attribute
    retcode = pSqlSetEnvAttr(m_henv, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleDiagnosticRecord(SQL_HANDLE_ENV, retcode);
        SQLFreeHandle(SQL_HANDLE_ENV, m_henv);
        return E_FAIL;
    }

    // Allocate connection handle
    retcode = pSqlAllocHandle(SQL_HANDLE_DBC, m_henv, &m_hdbc);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleDiagnosticRecord(SQL_HANDLE_ENV, retcode);
        pSqlFreeHandle(SQL_HANDLE_ENV, m_henv);
        m_henv = nullptr;
        return E_FAIL;
    }

    // Set login timeout to 5 seconds
    retcode = pSqlSetConnectAttr(m_hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleDiagnosticRecord(SQL_HANDLE_DBC, retcode);
        pSqlFreeHandle(SQL_HANDLE_ENV, m_henv);
        m_henv = nullptr;
        pSqlFreeHandle(SQL_HANDLE_ENV, m_hdbc);
        m_hdbc = nullptr;
        return E_FAIL;
    }

    // Enable bulk copy prior to connecting on allocated hdbc.
    retcode = pSqlSetConnectAttr(m_hdbc, SQL_COPT_SS_BCP, (SQLPOINTER)SQL_BCP_ON, SQL_IS_INTEGER);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleDiagnosticRecord(SQL_HANDLE_DBC, retcode);
        pSqlFreeHandle(SQL_HANDLE_ENV, m_henv);
        m_henv = nullptr;
        pSqlFreeHandle(SQL_HANDLE_ENV, m_hdbc);
        m_hdbc = nullptr;
        return E_FAIL;
    }

    // Connect to data source
    WCHAR szConnOut[255];
    SQLSMALLINT dwConnOut = 255;

    retcode = pSqlDriverConnect(
        m_hdbc,
        NULL,
        (SQLWCHAR*)strConnString.c_str(),
        (SQLSMALLINT)strConnString.size(),
        (SQLWCHAR*)szConnOut,
        (SQLSMALLINT)255,
        &dwConnOut,
        SQL_DRIVER_NOPROMPT);
    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        HandleDiagnosticRecord(SQL_HANDLE_DBC, retcode);
        pSqlFreeHandle(SQL_HANDLE_ENV, m_henv);
        m_henv = nullptr;
        pSqlFreeHandle(SQL_HANDLE_ENV, m_hdbc);
        m_hdbc = nullptr;
        m_bIsConnected = false;
        return E_FAIL;
    }
    else
    {
        HandleDiagnosticRecord(SQL_HANDLE_DBC, retcode);
        m_bIsConnected = true;
    }

    log::Verbose(_L_, L"VERBOSE: Successfully connected to SQL DSN %s\r\n", strConnString.c_str());
    return S_OK;
}

HRESULT Connection::CreateTable(
    const std::wstring& strTableName,
    const TableOutput::Schema& columns,
    bool bCompress,
    bool bTABLOCK)
{
    SQLRETURN retCode = 0;

    if (IsTablePresent(strTableName))
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);

    if (!columns)
        return E_POINTER;

    wstringstream stmt;

    stmt << L"CREATE TABLE [" << strTableName << L"] (" << endl;

    for (const auto& column : columns)
    {

        stmt << L"[" << column->ColumnName << L"] ";
        switch (column->Type)
        {
            case TableOutput::ColumnType::BoolType:
                stmt << L" BIT " << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                break;
            case TableOutput::ColumnType::UInt32Type:
                stmt << L" INT " << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                break;
            case TableOutput::ColumnType::UInt64Type:
                stmt << L" BIGINT " << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                break;
            case TableOutput::ColumnType::TimeStampType:
                stmt << L" DATETIME2 (3) " << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                break;
            case TableOutput::ColumnType::UTF8Type:
                if (column->dwLen.has_value())
                {
                    stmt << L" CHAR ( " << column->dwLen.value() << L" ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                else if (column->dwMaxLen.has_value())
                {
                    stmt << L" VARCHAR ( " << column->dwMaxLen.value() << L" ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                else
                {
                    stmt << L" VARCHAR ( MAX ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                break;
            case TableOutput::ColumnType::UTF16Type:
                if (column->dwLen.has_value())
                {
                    stmt << L" NCHAR ( " << column->dwLen.value() << L" ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                else if (column->dwMaxLen.has_value())
                {
                    stmt << L" NVARCHAR ( " << column->dwMaxLen.value() << L" ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                else
                {
                    stmt << L" NVARCHAR ( MAX ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                break;
            case TableOutput::ColumnType::BinaryType:
                if (column->dwLen.has_value())
                {
                    stmt << L" BINARY ( " << column->dwLen.value() << L" ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                else if (column->dwMaxLen.has_value())
                {
                    stmt << L" VARBINARY ( " << column->dwMaxLen.value() << L" ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                else
                {
                    stmt << L" VARBINARY ( MAX ) "
                         << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                }
                break;
            case TableOutput::ColumnType::GUIDType:
                stmt << L" BINARY  ( 16 )" << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                break;
            case TableOutput::ColumnType::XMLType:
                stmt << L" XML " << (column->bAllowsNullValues ? L"NULL" : L"NOT NULL") << L"," << endl;
                break;
            default:
                break;
        }
    }
    stmt << L")" << endl;

    if (bCompress)
    {
        stmt << L"WITH (DATA_COMPRESSION = PAGE)" << endl;
    }
    stmt << L";" << endl;

    HSTMT hstmt = NULL;
    retCode = pSqlAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }
    BOOST_SCOPE_EXIT(hstmt, this_) { this_->pSqlFreeHandle(SQL_HANDLE_STMT, hstmt); }
    BOOST_SCOPE_EXIT_END;

    wstring strStatement = stmt.str();
    retCode = pSqlExecDirect(hstmt, (SQLWCHAR*)strStatement.c_str(), SQL_NTS);
    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }

    if (bTABLOCK)
    {
        wstringstream stmt;
        stmt << L"EXEC sp_tableoption '" << strTableName << L"', 'table lock on bulk load', 'ON';" << endl;

        wstring strStatement = stmt.str();
        retCode = pSqlExecDirect(hstmt, (SQLWCHAR*)strStatement.c_str(), SQL_NTS);
        if (!SQLSuccess(retCode))
        {
            HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
            return E_FAIL;
        }
    }
    return S_OK;
}

HRESULT Connection::DropTable(const std::wstring& strTableName)
{
    SQLRETURN retCode = 0;

    if (!IsTablePresent(strTableName))
        return S_OK;

    HSTMT hstmt = NULL;
    retCode = pSqlAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }
    BOOST_SCOPE_EXIT(hstmt, this_) { this_->pSqlFreeHandle(SQL_HANDLE_STMT, hstmt); }
    BOOST_SCOPE_EXIT_END;

    wstring strStatement(L"DROP TABLE ");
    strStatement += strTableName;

    retCode = pSqlExecDirect(hstmt, (SQLWCHAR*)strStatement.c_str(), SQL_NTS);
    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }
    return S_OK;
}

HRESULT Connection::TruncateTable(const std::wstring& strTableName)
{
    SQLRETURN retCode = 0;

    if (!IsTablePresent(strTableName))
    {
        log::Warning(_L_, E_UNEXPECTED, L"Cannot truncate inexistant table %s\r\n", strTableName.c_str());
        return S_OK;
    }

    HSTMT hstmt = NULL;
    retCode = pSqlAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);

    BOOST_SCOPE_EXIT(hstmt, this_) { this_->pSqlFreeHandle(SQL_HANDLE_STMT, hstmt); }
    BOOST_SCOPE_EXIT_END;

    wstring strStatement(L"TRUNCATE TABLE ");

    strStatement += strTableName;

    retCode = pSqlExecDirect(hstmt, (SQLWCHAR*)strStatement.c_str(), SQL_NTS);

    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT Connection::ExecuteStatement(const std::wstring& strStatement)
{
    HSTMT hstmt = NULL;
    SQLRETURN retCode = pSqlAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }
    BOOST_SCOPE_EXIT(hstmt, this_) { this_->pSqlFreeHandle(SQL_HANDLE_STMT, hstmt); }
    BOOST_SCOPE_EXIT_END;

    retCode = pSqlExecDirect(hstmt, (SQLWCHAR*)strStatement.c_str(), (SQLINTEGER)strStatement.size());

    if (!SQLSuccess(retCode))
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
        return E_FAIL;
    }
    return S_OK;
}

bool Connection::IsTablePresent(const std::wstring& strTableName)
{
    struct DataBinding
    {
        SQLSMALLINT TargetType;
        SQLPOINTER TargetValuePtr;
        SQLINTEGER BufferLength;
        SQLLEN StrLen_or_Ind;
    };

    // allocate memory for the binding
    // free this memory when done
    int bufferSize = 1024, i, numCols = 5;
    struct DataBinding* catalogResult = (struct DataBinding*)malloc(numCols * sizeof(struct DataBinding));
    if (catalogResult == nullptr)
        return false;

    for (i = 0; i < numCols; i++)
    {
        catalogResult[i].TargetType = SQL_C_WCHAR;
        catalogResult[i].BufferLength = (bufferSize + 1);
        catalogResult[i].TargetValuePtr = malloc(sizeof(unsigned char) * catalogResult[i].BufferLength);
    }
    BOOST_SCOPE_EXIT(catalogResult, numCols)
    {
        for (int i = 0; i < numCols; i++)
        {
            if (catalogResult[i].TargetValuePtr != nullptr)
                free(catalogResult[i].TargetValuePtr);
        }
        free(catalogResult);
    }
    BOOST_SCOPE_EXIT_END;

    for (int i = 0; i < numCols; i++)
    {
        if (catalogResult[i].TargetValuePtr == nullptr)
            return false;
    }

    HSTMT hstmt = NULL;
    SQLRETURN retCode = pSqlAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
    if (!SQLSuccess(retCode) && retCode != SQL_NO_DATA)
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
    }

    BOOST_SCOPE_EXIT(hstmt, this_) { this_->pSqlFreeHandle(SQL_HANDLE_STMT, hstmt); }
    BOOST_SCOPE_EXIT_END;

    // setup the binding (can be used even if the statement is closed by closeStatementHandle)
    for (i = 0; i < numCols; i++)
        SQLBindCol(
            hstmt,
            (SQLUSMALLINT)i + 1,
            catalogResult[i].TargetType,
            catalogResult[i].TargetValuePtr,
            catalogResult[i].BufferLength,
            &(catalogResult[i].StrLen_or_Ind));

    retCode = SQLTables(hstmt, NULL, 0, L"dbo", SQL_NTS, NULL, 0, L"TABLE", SQL_NTS);
    if (!SQLSuccess(retCode) && retCode != SQL_NO_DATA)
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
    }

    for (retCode = SQLFetch(hstmt); SQLSuccess(retCode); retCode = SQLFetch(hstmt))
    {
        if (catalogResult[2].StrLen_or_Ind != SQL_NULL_DATA)
        {
            if (equalCaseInsensitive(strTableName, (WCHAR*)catalogResult[2].TargetValuePtr))
                return true;
        }
    }

    if (!SQLSuccess(retCode) && retCode != SQL_NO_DATA)
    {
        HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retCode);
    }
    return false;
}

STDMETHODIMP Connection::LoadSqlNativeClient(LPCWSTR szSqlLib, LPCWSTR szODBCLib)
{
    if (bSqlNativeInitialized)
        return S_OK;
    HRESULT hr = E_FAIL;

    hSqlNcli = LoadLibraryEx(szSqlLib, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (hSqlNcli == NULL)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to load sql native client using %s path\r\n",
            szSqlLib);
        return hr;
    }
    else
    {
        log::Verbose(_L_, L"SqlWriter: Loaded %s successfully\r\n", szSqlLib);
    }

    hODBC = LoadLibraryEx(szODBCLib, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (hODBC == NULL)
    {
        log::Error(
            _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to load ODBC client using %s path\r\n", szODBCLib);
        return hr;
    }
    else
    {
        log::Verbose(_L_, L"SqlWriter: Loaded %s successfully\r\n", szODBCLib);
    }

#define LOAD_SQLENTRY(Module, Type, Variable, Name)                                                                    \
    Variable = (Type)GetProcAddress(Module, Name);                                                                     \
    if (Variable == NULL)                                                                                              \
    {                                                                                                                  \
        hr = HRESULT_FROM_WIN32(GetLastError());                                                                       \
        log::Error(_L_, hr, L"Could not resolve %S\r\n", Name);                                                        \
        FreeLibrary(hSqlNcli);                                                                                         \
        hSqlNcli = NULL;                                                                                               \
        FreeLibrary(hODBC);                                                                                            \
        hODBC = NULL;                                                                                                  \
        return hr;                                                                                                     \
    }

    LOAD_SQLENTRY(hODBC, PSQLALLOCHANDE, pSqlAllocHandle, "SQLAllocHandle");
    LOAD_SQLENTRY(hODBC, PSQLFREEHANDLE, pSqlFreeHandle, "SQLFreeHandle");
    LOAD_SQLENTRY(hODBC, PSQLDIAGRECW, pSqlDiagRec, "SQLGetDiagRecW");
    LOAD_SQLENTRY(hODBC, PSQLSETENVATTR, pSqlSetEnvAttr, "SQLSetEnvAttr");
    LOAD_SQLENTRY(hODBC, PSQLSETCONNECTATTRW, pSqlSetConnectAttr, "SQLSetConnectAttrW");
    LOAD_SQLENTRY(hODBC, PSQLDRIVERCONNECTW, pSqlDriverConnect, "SQLDriverConnectW");
    LOAD_SQLENTRY(hODBC, PSQLDISCONNECT, pSqlDisconnect, "SQLDisconnect");
    LOAD_SQLENTRY(hODBC, PSQLBINDPARAMETER, pSqlBindParameter, "SQLBindParameter");
    LOAD_SQLENTRY(hODBC, PSQLEXECDIRECT, pSqlExecDirect, "SQLExecDirectW");
    LOAD_SQLENTRY(hSqlNcli, PBCP_INITW, pbcp_init, "bcp_initW");
    LOAD_SQLENTRY(hSqlNcli, PBCP_BIND, pbcp_bind, "bcp_bind");
    LOAD_SQLENTRY(hSqlNcli, PBCP_SENDROW, pbcp_sendrow, "bcp_sendrow");
    LOAD_SQLENTRY(hSqlNcli, PBCP_BATCH, pbcp_batch, "bcp_batch");
    LOAD_SQLENTRY(hSqlNcli, PBCP_DONE, pbcp_done, "bcp_done");

#undef LOAD_SQLENTRY

    log::Verbose(_L_, L"SqlWriter: Initialized successfully\r\n");
    bSqlNativeInitialized = true;

    return S_OK;
}

bool Connection::SQLSuccess(SQLRETURN retCode)
{
    return (retCode == SQL_SUCCESS || retCode == SQL_SUCCESS_WITH_INFO);
    ;
}

void Connection::HandleDiagnosticRecord(SQLSMALLINT hType, RETCODE RetCode)
{
    SQLHANDLE hHandle = NULL;

    if (hType == SQL_HANDLE_DBC)
        hHandle = m_hdbc;
    else if (hType == SQL_HANDLE_ENV)
        hHandle = m_henv;
    else
    {
        log::Error(_L_, E_INVALIDARG, L"Invalid hType provided, must be either SQL_HANDLE_DBC or SQL_HANDLE_ENV\r\n");
        return;
    }

    return HandleDiagnosticRecord(hHandle, hType, RetCode);
}

void Connection::HandleDiagnosticRecord(SQLHANDLE Handle, SQLSMALLINT hType, RETCODE RetCode)
{
    if (!bSqlNativeInitialized)
        return;

    if (RetCode == SQL_INVALID_HANDLE)
    {
        log::Error(_L_, E_FAIL, L"Invalid SqlWriter handle!\r\n");
        return;
    }

    SQLSMALLINT iRec = 0;
    SQLINTEGER iError;
    WCHAR wszMessage[1000];
    WCHAR wszState[SQL_SQLSTATE_SIZE + 1];

    while (pSqlDiagRec(
               hType,
               Handle,
               ++iRec,
               wszState,
               &iError,
               wszMessage,
               (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)),
               (SQLSMALLINT*)NULL)
           == SQL_SUCCESS)
    {
        // Hide data truncated..
        if (wcsncmp(wszState, L"01004", 5))
        {
            if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
            {
                log::Error(_L_, E_FAIL, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
            }
            else
            {
                log::Verbose(_L_, L"INFO: [%5.5s] %s (%d)\n", wszState, wszMessage, iError);
            }
        }
    }
    return;
}

STDMETHODIMP Connection::Disconnect()
{
    if (!bSqlNativeInitialized)
        return E_FAIL;

    if (m_hdbc != nullptr)
        pSqlDisconnect(m_hdbc);
    if (m_hdbc != nullptr)
    {
        pSqlFreeHandle(SQL_HANDLE_DBC, m_hdbc);
        m_hdbc = nullptr;
    }
    if (m_henv != nullptr)
    {
        pSqlFreeHandle(SQL_HANDLE_ENV, m_henv);
        m_henv = nullptr;
    }
    m_bIsConnected = false;
    return S_OK;
}

Connection::~Connection()
{
    Disconnect();
}
