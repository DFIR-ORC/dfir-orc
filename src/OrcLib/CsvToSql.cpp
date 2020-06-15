//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CsvToSql.h"

#include "LogFileWriter.h"

#include "CSVFileReader.h"
#include "TableOutput.h"

using namespace std;
using namespace Orc;

CsvToSql::CsvToSql(logger pLog)
    : _L_(std::move(pLog))
{
    m_pReader = nullptr;
    m_pSqlWriter = nullptr;
}

HRESULT CsvToSql::Initialize(
    std::shared_ptr<TableOutput::CSV::FileReader> pReader,
    std::shared_ptr<TableOutput::IConnectWriter> pSql)
{
    if (!pReader->IsFileOpened())
    {
        log::Error(_L_, E_INVALIDARG, L"CsvToSql can only be initialized with a valid CSV file opened\r\n");
        return E_INVALIDARG;
    }

    if (pReader->GetSchema().Column.empty())
    {
        log::Error(_L_, E_INVALIDARG, L"CSV file reader does not have any schema defined, needed!\r\n");
        return E_INVALIDARG;
    }

    if (pSql->GetColumnDefinitions().empty())
    {
        log::Error(_L_, E_INVALIDARG, L"CsvToSql's SQL database writer requires a valid schema defined\r\n");
        return E_INVALIDARG;
    }

    const TableOutput::BoundRecord& sql_schema = pSql->GetColumnDefinitions();
    m_mappings.resize(sql_schema.size());

    const auto& csv_schema = pReader->GetSchema().Column;

    for (const auto& sql_column : sql_schema)
    {

        std::vector<DWORD>& mappings = m_mappings;

        for (const auto& csv_column : csv_schema)
        {
            if (!wcscmp(csv_column.Name.c_str(), sql_column.ColumnName.c_str()))
            {
                mappings[csv_column.Index] = sql_column.dwColumnID;
            }
        }
    }

    std::swap(m_pReader, pReader);
    std::swap(m_pSqlWriter, pSql);
    return S_OK;
}

HRESULT CsvToSql::MoveColumn(const TableOutput::CSV::FileReader::Column& csv_value, TableOutput::BoundColumn& sql_value)
{
    switch (csv_value.Definition->Type)
    {
        case TableOutput::CSV::FileReader::UnknownType:
            return S_OK;
        case TableOutput::CSV::FileReader::String:
            return sql_value.WriteCharArray(csv_value.String.szString, csv_value.String.dwLength);
        case TableOutput::CSV::FileReader::DateTime:
            return sql_value.WriteFileTime(csv_value.ftDateTime);
        case TableOutput::CSV::FileReader::Integer:
            return sql_value.WriteInteger(csv_value.dwInteger);
        case TableOutput::CSV::FileReader::LargeInteger:
            return sql_value.WriteInteger(csv_value.liLargeInteger.QuadPart);
        case TableOutput::CSV::FileReader::GUIDType:
            return sql_value.WriteGUID(csv_value.guid);
        case TableOutput::CSV::FileReader::Boolean:
            return sql_value.WriteBool(csv_value.boolean);
        default:
            return E_NOTIMPL;
    }
}

HRESULT CsvToSql::MoveData(const TableOutput::CSV::FileReader::Record& csv_record, BoundRecord& sql_record)
{
    std::for_each(
        ++begin(csv_record.Values),
        end(csv_record.Values),
        [this, &sql_record](const TableOutput::CSV::FileReader::Column& column) {
            if (FAILED(MoveColumn(column, sql_record[m_mappings[column.Definition->Index]])))
            {
                m_pSqlWriter->AbandonColumn();
            }
            else
                m_pSqlWriter->NextColumn();
        });

    return S_OK;
}

HRESULT CsvToSql::MoveNextLine(TableOutput::CSV::FileReader::Record& record)
{
    HRESULT hr = E_FAIL;

    if (SUCCEEDED(hr = m_pReader->ParseNextLine(record)))
    {
        if (FAILED(hr = MoveData(record, m_pSqlWriter->GetColumnDefinitions())))
            return hr;

        if (FAILED(hr = m_pSqlWriter->WriteEndOfLine()))
            return hr;
    }
    else
        return hr;

    return hr;
}

CsvToSql::~CsvToSql(void) {}
