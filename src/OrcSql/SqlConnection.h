//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcSql.h"

#include "TableOutputWriter.h"

#include <sql.h>

#include <sqlncli.h>

typedef SQLRETURN(
    SQL_API* PSQLALLOCHANDE)(SQLSMALLINT HandleType, SQLHANDLE InputHandle, __out SQLHANDLE* OutputHandle);
typedef SQLRETURN(SQL_API* PSQLFREEHANDLE)(SQLSMALLINT HandleType, SQLHANDLE Handle);

typedef SQLRETURN(SQL_API* PSQLDIAGRECW)(
    SQLSMALLINT fHandleType,
    SQLHANDLE handle,
    SQLSMALLINT iRecord,
    __out_ecount_opt(6) SQLWCHAR* szSqlState,
    SQLINTEGER* pfNativeError,
    __out_ecount_opt(cchErrorMsgMax) SQLWCHAR* szErrorMsg,
    SQLSMALLINT cchErrorMsgMax,
    SQLSMALLINT* pcchErrorMsg);

typedef SQLRETURN(SQL_API* PSQLSETENVATTR)(
    SQLHENV EnvironmentHandle,
    SQLINTEGER Attribute,
    __in_bcount(StringLength) SQLPOINTER Value,
    SQLINTEGER StringLength);

typedef SQLRETURN(SQL_API* PSQLSETCONNECTATTRW)(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    __in_bcount_opt(cbValue) SQLPOINTER rgbValue,
    SQLINTEGER cbValue);

typedef SQLRETURN(SQL_API* PSQLDRIVERCONNECTW)(
    SQLHDBC hdbc,
    SQLHWND hwnd,
    __in_ecount(cchConnStrIn) SQLWCHAR* szConnStrIn,
    SQLSMALLINT cchConnStrIn,
    __out_ecount_opt(cchConnStrOutMax) SQLWCHAR* szConnStrOut,
    SQLSMALLINT cchConnStrOutMax,
    __out_opt SQLSMALLINT* pcchConnStrOut,
    SQLUSMALLINT fDriverCompletion);
typedef SQLRETURN(SQL_API* PSQLDISCONNECT)(SQLHDBC ConnectionHandle);

typedef SQLRETURN(SQL_API* PSQLBINDPARAMETER)(
    SQLHSTMT StatementHandle,
    SQLUSMALLINT ParameterNumber,
    SQLSMALLINT InputOutputType,
    SQLSMALLINT ValueType,
    SQLSMALLINT ParameterType,
    SQLULEN ColumnSize,
    SQLSMALLINT DecimalDigits,
    SQLPOINTER ParameterValuePtr,
    SQLLEN BufferLength,
    SQLLEN* StrLen_or_IndPtr);
typedef SQLRETURN(SQL_API* PSQLEXECDIRECT)(SQLHSTMT hstmt, SQLWCHAR* StatementText, SQLINTEGER TextLength);

typedef RETCODE(SQL_API* PBCP_INITW)(HDBC, LPCWSTR, LPCWSTR, LPCWSTR, INT);
typedef RETCODE(SQL_API* PBCP_BIND)(HDBC, const BYTE*, INT, DBINT, const BYTE*, INT, INT, INT);
typedef RETCODE(SQL_API* PBCP_SENDROW)(HDBC);
typedef DBINT(SQL_API* PBCP_BATCH)(HDBC);
typedef DBINT(SQL_API* PBCP_DONE)(HDBC);

namespace Orc {

class LogFileWriter;

namespace TableOutput::Sql {

class Connection : public TableOutput::IConnection
{
private:
    STDMETHOD(Initialize)();

public:
    Connection(logger pLog)
        : _L_(std::move(pLog)) {};

    STDMETHOD(Connect)(const std::wstring& strConnString) override final;
    STDMETHOD_(bool, IsConnected)() const override final { return m_bIsConnected; };

    STDMETHOD_(bool, IsTablePresent)(const std::wstring& strTableName) override final;
    STDMETHOD(DropTable)(const std::wstring& strTableName) override final;
    STDMETHOD(TruncateTable)(const std::wstring& strTableName) override final;
    STDMETHOD(CreateTable)
    (const std::wstring& strTableName,
     const TableOutput::Schema& columns,
     bool bCompress = false,
     bool bTABLOCK = true) override final;

    STDMETHOD(ExecuteStatement)(const std::wstring& strStatement) override final;

    SQLRETURN SqlAllocHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle, SQLHANDLE* OutputHandlePtr)
    {
        return pSqlAllocHandle(HandleType, InputHandle, OutputHandlePtr);
    }
    SQLRETURN SqlFreeHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle)
    {
        return pSqlFreeHandle(HandleType, InputHandle);
    }
    SQLRETURN SqlDiagRec(
        SQLSMALLINT fHandleType,
        SQLHANDLE handle,
        SQLSMALLINT iRecord,
        __out_ecount_opt(6) SQLWCHAR* szSqlState,
        SQLINTEGER* pfNativeError,
        __out_ecount_opt(cchErrorMsgMax) SQLWCHAR* szErrorMsg,
        SQLSMALLINT cchErrorMsgMax,
        SQLSMALLINT* pcchErrorMsg)
    {
        return pSqlDiagRec(
            fHandleType, handle, iRecord, szSqlState, pfNativeError, szErrorMsg, cchErrorMsgMax, pcchErrorMsg);
    }
    SQLRETURN SqlSetEnvAttr(SQLINTEGER Attribute, __in_bcount(StringLength) SQLPOINTER Value, SQLINTEGER StringLength)
    {
        return pSqlSetEnvAttr(m_henv, Attribute, Value, StringLength);
    }
    SQLRETURN SqlSetConnectAttr(SQLINTEGER fAttribute, __in_bcount_opt(cbValue) SQLPOINTER rgbValue, SQLINTEGER cbValue)
    {
        return pSqlSetConnectAttr(m_hdbc, fAttribute, rgbValue, cbValue);
    }
    SQLRETURN SqlDriverConnect(
        SQLHWND hwnd,
        __in_ecount(cchConnStrIn) SQLWCHAR* szConnStrIn,
        SQLSMALLINT cchConnStrIn,
        __out_ecount_opt(cchConnStrOutMax) SQLWCHAR* szConnStrOut,
        SQLSMALLINT cchConnStrOutMax,
        __out_opt SQLSMALLINT* pcchConnStrOut,
        SQLUSMALLINT fDriverCompletion)
    {
        return pSqlDriverConnect(
            m_hdbc, hwnd, szConnStrIn, cchConnStrIn, szConnStrOut, cchConnStrOutMax, pcchConnStrOut, fDriverCompletion);
    }
    SQLRETURN SqlDisconnect() { return pSqlDisconnect(m_hdbc); }

    SQLRETURN SqlBindParameter(
        SQLHSTMT StatementHandle,
        SQLUSMALLINT ParameterNumber,
        SQLSMALLINT InputOutputType,
        SQLSMALLINT ValueType,
        SQLSMALLINT ParameterType,
        SQLULEN ColumnSize,
        SQLSMALLINT DecimalDigits,
        SQLPOINTER ParameterValuePtr,
        SQLLEN BufferLength,
        SQLLEN* StrLen_or_IndPtr)
    {
        return pSqlBindParameter(
            StatementHandle,
            ParameterNumber,
            InputOutputType,
            ValueType,
            ParameterType,
            ColumnSize,
            DecimalDigits,
            ParameterValuePtr,
            BufferLength,
            StrLen_or_IndPtr);
    }
    SQLRETURN SqlExecDirect(SQLHSTMT hstmt, SQLWCHAR* StatementText, SQLINTEGER TextLength)
    {
        return pSqlExecDirect(hstmt, StatementText, TextLength);
    }

    RETCODE bcp_init(LPCWSTR szTable, LPCWSTR szDataFile, LPCWSTR szErrorFile, INT eDirection)
    {
        return pbcp_init(m_hdbc, szTable, szDataFile, szErrorFile, eDirection);
    }
    RETCODE
    bcp_bind(LPCBYTE pData, INT cbIndicator, DBINT cbData, LPCBYTE pTerm, INT cbTerm, INT eDataType, INT idxServerCol)
    {
        return pbcp_bind(m_hdbc, pData, cbIndicator, cbData, pTerm, cbTerm, eDataType, idxServerCol);
    }
    RETCODE bcp_sendrow() { return pbcp_sendrow(m_hdbc); }
    DBINT bcp_batch() { return pbcp_batch(m_hdbc); }
    DBINT bcp_done() { return pbcp_done(m_hdbc); }

    void HandleDiagnosticRecord(SQLSMALLINT hType, RETCODE RetCode);
    void HandleDiagnosticRecord(SQLHANDLE Handle, SQLSMALLINT hType, RETCODE RetCode);

    STDMETHOD(Disconnect)();

    ~Connection();

private:
    HMODULE hSqlNcli = NULL;
    HMODULE hODBC = NULL;

    PSQLALLOCHANDE pSqlAllocHandle = NULL;
    PSQLFREEHANDLE pSqlFreeHandle = NULL;
    PSQLDIAGRECW pSqlDiagRec = NULL;
    PSQLSETENVATTR pSqlSetEnvAttr = NULL;

    PSQLSETCONNECTATTRW pSqlSetConnectAttr = NULL;
    PSQLDRIVERCONNECTW pSqlDriverConnect = NULL;
    PSQLDISCONNECT pSqlDisconnect = NULL;

    PSQLEXECDIRECT pSqlExecDirect = NULL;
    PSQLBINDPARAMETER pSqlBindParameter = NULL;

    PBCP_INITW pbcp_init = NULL;
    PBCP_BIND pbcp_bind = NULL;
    PBCP_SENDROW pbcp_sendrow = NULL;
    PBCP_BATCH pbcp_batch = NULL;
    PBCP_DONE pbcp_done = NULL;

    bool bSqlNativeInitialized = false;

    SQLHENV m_henv = NULL;
    SQLHDBC m_hdbc = NULL;
    bool m_bIsConnected = false;

    logger _L_;

    STDMETHOD(LoadSqlNativeClient)(LPCWSTR szSqlLib = L"SQLNCLI11.DLL", LPCWSTR szODBCLib = L"ODBC32.DLL");

    bool SQLSuccess(SQLRETURN retCode);
};

}  // namespace TableOutput::Sql

}  // namespace Orc
