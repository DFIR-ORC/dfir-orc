//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OrcException.h"

#include "OutputSpec.h"

#include "ConfigItem.h"
#include "ParameterCheck.h"

#include "TableOutput.h"
#include "TableOutputWriter.h"
#include "SqlOutputWriter.h"
#include "ParquetOutputWriter.h"
#include "ApacheOrcOutputWriter.h"
#include "CsvFileWriter.h"

#include "CaseInsensitive.h"

#include "FileStream.h"
#include "PipeStream.h"

#include <boost/tokenizer.hpp>

#include <spdlog/spdlog.h>

using namespace std;
using namespace boost;

using namespace Orc;
using namespace Orc::TableOutput;

std::shared_ptr<IWriter> Orc::TableOutput::GetWriter(const OutputSpec& out)
{
    HRESULT hr = E_FAIL;

    switch (out.Type)
    {
        case OutputSpec::Kind::None:
        case OutputSpec::Kind::Directory:
        case OutputSpec::Kind::Archive:
            return nullptr;
        case OutputSpec::Kind::TableFile:
        case OutputSpec::Kind::CSV:
        case OutputSpec::Kind::TSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV: {
            auto options = std::make_unique<CSV::Options>();

            options->Encoding = out.OutputEncoding;
            options->bBOM = true;
            options->Delimiter = out.szSeparator;
            options->StringDelimiter = out.szQuote;

            auto retval = CSV::Writer::MakeNew(std::move(options));

            if (!retval)
            {
                spdlog::error(L"CSV File format is not available");
                return nullptr;
            }

            if (FAILED(hr = retval->WriteToFile(out.Path)))
            {
                spdlog::error(L"Could not create specified file: '{}' (code: {:#x})", out.Path, hr);
                return nullptr;
            }

            if (out.Schema)
            {
                if (FAILED(hr = retval->SetSchema(out.Schema)))
                {
                    spdlog::error(L"Could not set schema to CSV file: '{}' (code: {:#x})", out.Path, hr);
                    return nullptr;
                }
            }

            return retval;
        }
        case OutputSpec::Kind::Parquet:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet: {
            auto options = std::make_unique<TableOutput::Options>();

            auto pWriter = GetParquetWriter(std::move(options));

            if (!pWriter)
            {
                spdlog::error(L"Parquet File format is not available");
                return nullptr;
            }

            if (out.Schema)
            {
                if (FAILED(hr = pWriter->SetSchema(out.Schema)))
                {
                    spdlog::error(L"Could not write columns to parquet file: {} (code: {:#x})", out.Path, hr);
                    return nullptr;
                }
            }

            if (FAILED(hr = pWriter->WriteToFile(out.Path)))
            {
                spdlog::error(L"Could not create specified file: {} (code: {:#x})", out.Path, hr);
                return nullptr;
            }
            return pWriter;
        }
        case OutputSpec::Kind::ORC:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC: {
            auto options = std::make_unique<TableOutput::Options>();

            auto pWriter = GetApacheOrcWriter(std::move(options));

            if (!pWriter)
            {
                spdlog::error(L"Parquet File format is not available");
                return nullptr;
            }

            if (out.Schema)
            {
                if (FAILED(hr = pWriter->SetSchema(out.Schema)))
                {
                    spdlog::error(L"Could not write columns to parquet file {} (code: {:#x})", out.Path, hr);
                    return nullptr;
                }
            }

            if (FAILED(hr = pWriter->WriteToFile(out.Path)))
            {
                spdlog::error(L"Could not create specified file: {} (code: {:#x})", out.Path, hr);
                return nullptr;
            }
            return pWriter;
        }
        case OutputSpec::Kind::SQL: {
            auto options = std::make_unique<TableOutput::Options>();

            auto pSqlConnection = GetSqlConnection(std::move(options));

            if (!pSqlConnection)
            {
                spdlog::error("SQLServer connectivity is not available");
                return nullptr;
            }

            if (FAILED(hr = pSqlConnection->Connect(out.ConnectionString)))
            {
                spdlog::error(L"Could not connect to SQL '{}' (code: {:#x})", out.ConnectionString, hr);
                return nullptr;
            }

            switch (out.disposition)
            {
                case OutputSpec::Disposition::Append:
                    break;
                case OutputSpec::Disposition::Truncate:
                    if (FAILED(hr = pSqlConnection->TruncateTable(out.TableName)))
                    {
                        spdlog::error(L"Failed to truncate table '{}' (code: {:#x})", out.TableName, hr);
                    }
                    break;
                case OutputSpec::Disposition::CreateNew: {
                    if (pSqlConnection->IsTablePresent(out.TableName))
                    {
                        if (FAILED(hr = pSqlConnection->DropTable(out.TableName)))
                        {
                            spdlog::error(L"Failed to drop table '{}' (code: {:#x})", out.TableName, hr);
                        }
                    }
                    if (out.Schema)
                    {
                        if (FAILED(hr = pSqlConnection->CreateTable(out.TableName, out.Schema)))
                        {
                            spdlog::error(L"Failed to create table '{}' (code: {:#x})", out.TableName, hr);
                        }
                    }
                    else
                    {
                        spdlog::error(
                            L"Failed to create table {} (code: {:#x}) -> No column definition available",
                            out.TableName,
                            hr);
                    }
                }
                break;
                default:
                    break;
            }
            auto pSqlWriter = GetSqlWriter(std::move(options));

            if (FAILED(hr = pSqlWriter->SetConnection(pSqlConnection)))
            {
                spdlog::error(L"Could not connect to SQL '{}' (code: {:#x}", out.ConnectionString, hr);
                return nullptr;
            }

            if (out.Schema)
            {
                if (FAILED(hr = pSqlWriter->SetSchema(out.Schema)))
                {
                    spdlog::error(L"Could not add SQL columns (code: {:#x})", hr);
                    return nullptr;
                }
                if (FAILED(hr = pSqlWriter->BindColumns(out.TableName.c_str())))
                {
                    spdlog::error(L"Could not bind SQL columns (code: {:#x})", hr);
                    return nullptr;
                }
            }
            else
            {
                spdlog::error("Could not bind SQL columns, not column schema defined");
                return nullptr;
            }

            return pSqlWriter;
        }
        default:
            spdlog::error("Invalid type of output to create SecDescrWriter");
            return nullptr;
    }
}

std::shared_ptr<IWriter> Orc::TableOutput::GetWriter(LPCWSTR szFileName, const OutputSpec& out)
{
    HRESULT hr = E_FAIL;

    std::shared_ptr<ByteStream> pStream;

    switch (out.Type)
    {
        case OutputSpec::Kind::None:
        case OutputSpec::Kind::CSV:
        case OutputSpec::Kind::TSV:
        case OutputSpec::Kind::Parquet:
        case OutputSpec::Kind::ORC:
        case OutputSpec::Kind::TableFile:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet:
        case OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC:
        case OutputSpec::Kind::SQL:
            spdlog::error("Invalid type of output to create suffixed writer");
            return nullptr;
        case OutputSpec::Kind::Directory: {
            std::wstring strFilePath = out.Path + L"\\" + szFileName;

            auto pFileStream = std::make_shared<FileStream>();

            if (FAILED(hr = pFileStream->WriteTo(strFilePath.c_str())))
            {
                spdlog::error(L"Could not create output file: '{} (code: {:#x})'", strFilePath, hr);
                return nullptr;
            }
            pStream = pFileStream;
        }
        break;
        case OutputSpec::Kind::Archive: {
            auto pPipe = std::make_shared<PipeStream>();

            if (FAILED(hr = pPipe->CreatePipe()))
            {
                spdlog::error(L"Could not create output pipe for output '{}' (code: {:#x})", szFileName, hr);
                return nullptr;
            }
            pStream = pPipe;
        }
        break;
        default:
            return nullptr;
    }

    auto options = std::make_unique<TableOutput::CSV::Options>();
    options->Encoding = out.OutputEncoding;

    auto retval = TableOutput::CSV::Writer::MakeNew(std::move(options));

    if (FAILED(hr = retval->WriteToStream(pStream)))
    {
        spdlog::error(L"Could not write to byte stream (code: {:#x})", hr);
        return nullptr;
    }

    if (out.Schema)
    {
        if (FAILED(hr = retval->SetSchema(out.Schema)))
        {
            spdlog::error(L"Could not write columns to output file (code: {:#x})", hr);
            return nullptr;
        }
    }
    else
    {
        spdlog::error(L"Could not write columns to output file: no column schema defined");
        return nullptr;
    }
    return retval;
}

std::shared_ptr<IConnection> Orc::TableOutput::GetSqlConnection(std::unique_ptr<Options> options)
{
    static auto extension = Orc::ExtensionLibrary::GetLibrary<SqlOutputWriter>();

    if (!extension)
        return nullptr;

    return extension->ConnectionFactory(std::move(options));
}

std::shared_ptr<IStreamWriter> Orc::TableOutput::GetCSVWriter(std::unique_ptr<Options> options)
{
    auto retval = Orc::TableOutput::CSV::Writer::MakeNew(std::move(options));

    if (!retval)
    {
        spdlog::error("CSV File format is not available");
        return nullptr;
    }
    return retval;
}

std::shared_ptr<IConnectWriter> Orc::TableOutput::GetSqlWriter(std::unique_ptr<Options> options)
{
    static auto extension = Orc::ExtensionLibrary::GetLibrary<SqlOutputWriter>();

    if (!extension)
        return nullptr;

    return extension->ConnectTableFactory(std::move(options));
}

std::shared_ptr<IStreamWriter> Orc::TableOutput::GetParquetWriter(std::unique_ptr<Options> options)
{
    static auto extension = Orc::ExtensionLibrary::GetLibrary<ParquetOutputWriter>();

    if (!extension)
        return nullptr;

    return extension->StreamTableFactory(std::move(options));
}

std::shared_ptr<IStreamWriter> Orc::TableOutput::GetApacheOrcWriter(std::unique_ptr<Options> options)
{
    static auto extension = Orc::ExtensionLibrary::GetLibrary<ApacheOrcOutputWriter>();

    if (!extension)
        return nullptr;

    return extension->StreamTableFactory(std::move(options));
}

TableOutput::Schema Orc::TableOutput::GetColumnsFromConfig(const LPCWSTR szTableName, const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    Schema retval;

    const ConfigItem& tables = item.SubItems[CONFIG_SCHEMA_TABLE];

    auto it = begin(tables.NodeList);

    if (szTableName == nullptr && tables.NodeList.size() > 1)
    {
        for (; it != end(tables.NodeList); ++it)
        {
            if (!it->SubItems[CONFIG_SCHEMA_TABLEKEY])
            {
                break;
            }
        }

        if (it == end(tables.NodeList))
        {
            spdlog::warn("While loading schema: No table name specified and none of the table schemas have no name");
            return retval;
        }
    }
    else if (szTableName != nullptr)
    {
        for (; it != end(tables.NodeList); ++it)
        {
            if (it->SubItems[CONFIG_SCHEMA_TABLEKEY])
            {
                if (equalCaseInsensitive((const std::wstring&)it->SubItems[CONFIG_SCHEMA_TABLEKEY], szTableName))
                {
                    break;
                }
            }
        }
        if (it == end(tables.NodeList))
        {
            spdlog::error(L"while loading schema: No table name schema matches required name '{}'", szTableName);
            return retval;
        }
    }

    if (it == end(tables.NodeList))
        return retval;  // no sql schema defined!!

    using ColumnTypeConfig = std::vector<std::pair<DWORD, TableOutput::ColumnType>>;

    ColumnTypeConfig column_types = {
        {CONFIG_SCHEMA_COLUMN_BOOL, TableOutput::ColumnType::BoolType},
        {CONFIG_SCHEMA_COLUMN_UINT8, TableOutput::ColumnType::UInt8Type},
        {CONFIG_SCHEMA_COLUMN_INT8, TableOutput::ColumnType::Int8Type},
        {CONFIG_SCHEMA_COLUMN_UINT16, TableOutput::ColumnType::UInt16Type},
        {CONFIG_SCHEMA_COLUMN_INT16, TableOutput::ColumnType::Int16Type},
        {CONFIG_SCHEMA_COLUMN_UINT32, TableOutput::ColumnType::UInt32Type},
        {CONFIG_SCHEMA_COLUMN_INT32, TableOutput::ColumnType::Int32Type},
        {CONFIG_SCHEMA_COLUMN_UINT64, TableOutput::ColumnType::UInt64Type},
        {CONFIG_SCHEMA_COLUMN_INT64, TableOutput::ColumnType::Int64Type},
        {CONFIG_SCHEMA_COLUMN_TIMESTAMP, TableOutput::ColumnType::TimeStampType},
        {CONFIG_SCHEMA_COLUMN_UTF8, TableOutput::ColumnType::UTF8Type},
        {CONFIG_SCHEMA_COLUMN_UTF16, TableOutput::ColumnType::UTF16Type},
        {CONFIG_SCHEMA_COLUMN_BINARY, TableOutput::ColumnType::BinaryType},
        {CONFIG_SCHEMA_COLUMN_GUID, TableOutput::ColumnType::GUIDType},
        {CONFIG_SCHEMA_COLUMN_XML, TableOutput::ColumnType::XMLType},
        {CONFIG_SCHEMA_COLUMN_ENUM, TableOutput::ColumnType::EnumType},
        {CONFIG_SCHEMA_COLUMN_FLAGS, TableOutput::ColumnType::FlagsType}};

    using namespace std::string_view_literals;

    for (const auto& [item, type] : column_types)
    {
        const ConfigItem& columnlist = it->SubItems[item];
        if (!columnlist)
            continue;  // no column of this type

        if (columnlist.Type != ConfigItem::NODELIST)
            throw Orc::Exception(Fatal, L"Invalid column type item in configuration");

        for (const auto& column : columnlist.NodeList)
        {
            auto aCol = std::make_unique<TableOutput::Column>();
            aCol->dwColumnID = column.dwOrderIndex + 1;
            aCol->Type = type;

            if (column.SubItems[CONFIG_SCHEMA_COLUMN_NAME].Type != ConfigItem::ATTRIBUTE)
            {
                spdlog::warn(
                    L"'{}' is not an attribute and is ignored", column.SubItems[CONFIG_SCHEMA_COLUMN_NAME].strName);
                continue;
            }

            if (const auto& name = column.SubItems[CONFIG_SCHEMA_COLUMN_NAME])
                aCol->ColumnName = (std::wstring)name;

            if (const auto& description = column.SubItems[CONFIG_SCHEMA_COLUMN_DESCRIPTION])
                aCol->Description = (std::wstring)description;

            if (const auto& artifact = column.SubItems[CONFIG_SCHEMA_COLUMN_ARTIFACT])
                aCol->Artifact = (std::wstring)artifact;

            constexpr auto YES = L"yes"sv;
            constexpr auto NO = L"no"sv;

            if (const auto& null = column.SubItems[CONFIG_SCHEMA_COLUMN_NULL])
            {
                if (equalCaseInsensitive((const std::wstring&)null, YES, YES.size()))
                    aCol->bAllowsNullValues = true;
                else if (equalCaseInsensitive((const std::wstring&)null, NO, NO.size()))
                    aCol->bAllowsNullValues = false;
            }
            if (const auto& not_null = column.SubItems[CONFIG_SCHEMA_COLUMN_NOTNULL])
            {
                if (equalCaseInsensitive((const std::wstring&)not_null, YES, YES.size()))
                    aCol->bAllowsNullValues = false;
                else if (equalCaseInsensitive((const std::wstring&)not_null, NO, NO.size()))
                    aCol->bAllowsNullValues = true;
            }
            if (const auto& format = column.SubItems[CONFIG_SCHEMA_COLUMN_FMT])
            {
                aCol->Format = format;
            }

            try
            {
                // Handle types specificities
                switch (type)
                {
                    case TableOutput::ColumnType::BinaryType:
                        if (column.SubItems[CONFIG_SCHEMA_COLUMN_BINARY_LEN]
                            && column.SubItems[CONFIG_SCHEMA_COLUMN_BINARY_MAXLEN])
                        {
                            spdlog::error("len & maxlen cannot be used for the same binary column");
                            break;
                        }

                        if (const auto& len = column.SubItems[CONFIG_SCHEMA_COLUMN_BINARY_LEN])
                        {
                            aCol->Type = TableOutput::ColumnType::FixedBinaryType;
                            aCol->dwLen = (DWORD32)len;
                        }
                        else
                        {
                            if (const auto& maxlen = column.SubItems[CONFIG_SCHEMA_COLUMN_BINARY_MAXLEN])
                                aCol->dwMaxLen = (DWORD32)maxlen;
                        }
                        break;
                    case TableOutput::ColumnType::UTF8Type:
                        if (const auto& len = column.SubItems[CONFIG_SCHEMA_COLUMN_UTF8_LEN])
                            aCol->dwLen = (DWORD32)len;
                        else
                        {
                            if (const auto& maxlen = column.SubItems[CONFIG_SCHEMA_COLUMN_UTF8_MAXLEN])
                                aCol->dwMaxLen = (DWORD32)maxlen;
                        }
                        break;
                    case TableOutput::ColumnType::UTF16Type:
                        if (const auto& len = column.SubItems[CONFIG_SCHEMA_COLUMN_UTF16_LEN])
                            aCol->dwLen = (DWORD32)len;
                        else
                        {
                            if (const auto& maxlen = column.SubItems[CONFIG_SCHEMA_COLUMN_UTF16_MAXLEN])
                                aCol->dwMaxLen = (DWORD32)maxlen;
                        }
                        break;
                    case TableOutput::ColumnType::XMLType:
                        if (const auto& maxlen = column.SubItems[CONFIG_SCHEMA_COLUMN_XML_MAXLEN])
                            aCol->dwMaxLen = (DWORD32)maxlen;
                        break;
                    case TableOutput::ColumnType::EnumType:
                        if (const auto& values = column.SubItems[CONFIG_SCHEMA_COLUMN_ENUM_VALUE])
                        {
                            aCol->EnumValues.emplace();
                            DWORD dwIndex = 0L;
                            for (const auto& value : values.NodeList)
                            {
                                if (const auto& index = value[CONFIG_SCHEMA_COLUMN_ENUM_VALUE_INDEX])
                                    dwIndex = (DWORD32)index;
                                else
                                    dwIndex++;
                                aCol->EnumValues.value().push_back({(std::wstring)value, dwIndex});
                            }
                        }
                        break;
                    case TableOutput::ColumnType::FlagsType:
                        if (const auto& values = column.SubItems[CONFIG_SCHEMA_COLUMN_FLAGS_VALUE])
                        {
                            aCol->FlagsValues.emplace();
                            DWORD dwIndex = 0L;
                            for (const auto& value : values.NodeList)
                            {
                                if (const auto& index = value[CONFIG_SCHEMA_COLUMN_FLAGS_VALUE_INDEX])
                                    dwIndex = (DWORD32)index;
                                else
                                    dwIndex++;
                                aCol->FlagsValues.value().push_back({(std::wstring)value, dwIndex});
                            }
                        }
                        break;
                }
            }
            catch (const Orc::Exception& e)
            {
                if (e.IsCritical())
                    throw;

                spdlog::error(L"{} (code: {:#x})", e.Description, e.GetHRESULT());
            }
            catch (...)
            {
                throw;
            }

            retval.AddColumn(std::move(aCol));
        }
    }

    std::sort(begin(retval), end(retval), [](auto& left, auto& rigth) { return left->dwColumnID < rigth->dwColumnID; });

    return retval;
}
