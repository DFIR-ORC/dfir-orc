//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcSql.h"

#include <sql.h>

#include "TableOutput.h"
#include "BoundTableRecord.h"

namespace Orc {

class ConfigItem;

class CsvToSql;

namespace TableOutput::Sql {

class Connection;

class Writer
    : public ::Orc::TableOutput::Writer
    , public ::Orc::TableOutput::IConnectWriter
{
    friend class CsvToSql;

public:
    Writer(DWORD dwTransactionRowCount = 10000);

    STDMETHOD(SetConnection)(const std::shared_ptr<TableOutput::IConnection>& pConnection);
    ;

    virtual const TableOutput::Column& GetCurrentColumn() override final
    {
        if (m_Schema)
            return m_Schema[m_CurCol];
        else
            throw L"No schema associtated with Sql::Writer";
    }

    const BoundRecord& GetColumnDefinitions() const override final { return m_Columns; };
    BoundRecord& GetColumnDefinitions() override final { return m_Columns; };

    STDMETHOD(AddColumn)
    (DWORD dwColId,
     const std::wstring& strName,
     TableOutput::ColumnType type,
     std::optional<DWORD> dwMaxLen = std::nullopt,
     std::optional<DWORD> dwLen = std::nullopt);
    STDMETHOD(SetSchema)(const TableOutput::Schema& columns) override final;

    STDMETHOD(ClearTable)(LPCWSTR szTableName) override final;

    STDMETHOD(BindColumns)(LPCWSTR szTable) override final;
    STDMETHOD(NextColumn)() override final
    {
        m_CurCol++;
        return S_OK;
    }

    STDMETHOD(SendRow)();

    STDMETHOD(AbandonRow)();
    STDMETHOD(AbandonColumn)();

    STDMETHOD(Done)();

    STDMETHOD(Flush)();
    STDMETHOD(Close)();

    virtual DWORD GetCurrentColumnID() override final { return m_CurCol; };

    STDMETHOD(WriteNothing)() override final
    {
        m_CurCol++;
        return S_OK;
    };

    STDMETHOD(WriteString)(const std::wstring& strString) override final;
    STDMETHOD(WriteString)(const std::wstring_view& strString) override final;
    STDMETHOD(WriteString)(const WCHAR* szString) override final;
    STDMETHOD(WriteCharArray)(const WCHAR* szArray, DWORD dwCharCount) override final;

    STDMETHOD(WriteString)(const std::string& strString) override final;
    STDMETHOD(WriteString)(const std::string_view& strString) override final;
    STDMETHOD(WriteString)(const CHAR* szString) override final;
    STDMETHOD(WriteCharArray)(const CHAR* szArray, DWORD dwCharCount) override final;

protected:
    STDMETHOD(WriteFormated_)(const std::wstring_view& szFormat, IOutput::wformat_args args) override final;
    STDMETHOD(WriteFormated_)(const std::string_view& szFormat, IOutput::format_args args) override final;

public:
    STDMETHOD(WriteAttributes)(DWORD dwAttibutes) override final;

    STDMETHOD(WriteFileTime)(FILETIME fileTime) override final;
    STDMETHOD(WriteFileTime)(LONGLONG fileTime) override final;
    STDMETHOD(WriteTimeStamp)(time_t tmStamp) override final { return E_NOTIMPL; }
    STDMETHOD(WriteTimeStamp)(tm tmStamp) override final { return E_NOTIMPL; }

    STDMETHOD(WriteFileSize)(LARGE_INTEGER fileSize) override final;
    STDMETHOD(WriteFileSize)(ULONGLONG fileSize) override final;
    STDMETHOD(WriteFileSize)(DWORD nFileSizeHigh, DWORD nFileSizeLow) override final;

    STDMETHOD(WriteInteger)(DWORD dwInteger) override final;
    STDMETHOD(WriteInteger)(LONGLONG dw64Integer) override final;
    STDMETHOD(WriteInteger)(ULONGLONG dw64Integer) override final;

    STDMETHOD(WriteBytes)(const BYTE pSHA1[], DWORD dwLen) override final;
    STDMETHOD(WriteBytes)(const CBinaryBuffer& Buffer) override final;

    STDMETHOD(WriteBool)(bool bBoolean) override final;

    STDMETHOD(WriteEnum)(DWORD dwEnum) override final { return E_NOTIMPL; }
    STDMETHOD(WriteEnum)(DWORD dwEnum, const WCHAR* EnumValues[]) override final;
    STDMETHOD(WriteFlags)(DWORD dwFlags) override final { return E_NOTIMPL; };
    STDMETHOD(WriteFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final;
    STDMETHOD(WriteExactFlags)(DWORD dwFlags) override final { return E_NOTIMPL; };
    STDMETHOD(WriteExactFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[]) override final;

    STDMETHOD(WriteGUID)(const GUID& guid) override final;

    STDMETHOD(WriteXML)(const WCHAR* szString) override final;
    STDMETHOD(WriteXML)(const CHAR* szString) override final;
    STDMETHOD(WriteXML)(const WCHAR* szArray, DWORD dwCharCount) override final;
    STDMETHOD(WriteXML)(const CHAR* szArray, DWORD dwCharCount) override final;

    virtual HRESULT WriteEndOfLine() override final;

    ~Writer(void);

private:
    std::shared_ptr<Connection> m_pSQL;

    DWORD m_dwMaxTransactionRowCount = 0;

    DWORD m_dwRowsInTransaction = 0;
    DWORD m_dwWroteCount = 0;
    DWORD m_dwSentCount = 0;

    BoundRecord m_Columns;

    DWORD m_CurCol = (DWORD)-1;

    bool m_bBound = false;

    STDMETHOD(ClearBoundData)();
};
}  // namespace TableOutput::Sql
}  // namespace Orc
