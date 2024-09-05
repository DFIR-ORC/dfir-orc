//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#pragma warning(disable : 4995)
#pragma warning(disable : 4091)
#include <msclr\marshal.h>
#include <msclr\marshal_windows.h>
#include <msclr\com\ptr.h>
#pragma warning(default : 4995)
#pragma warning(default : 4091)

#include "LogParserDataReader.h"

#import "LogParser.dll" no_namespace raw_interfaces_only

using namespace System::Runtime::InteropServices;
using namespace System;
using namespace msclr;
using namespace msclr::interop;

namespace TaskUtils {

ref class LogQuery : public IDisposable
{
public:
    LogQuery() { lq.CreateInstance(__uuidof(LogQueryClass), 0, CLSCTX_INPROC_SERVER); }

    ~LogQuery() { this->!LogQuery(); }
    !LogQuery() { lq.Release(); }

private:
    com::ptr<ILogQuery> lq;
};

ref class LogRecord : public IDisposable
{
public:
    LogRecord(ILogRecord* ptr)
        : lr(ptr)
    {
    }

    ~LogRecord() { this->!LogRecord(); }
    !LogRecord() { lr.Release(); }

private:
    com::ptr<ILogRecord> lr;
};

ref class LogRecordset : public IDisposable
{

private:
    int INTEGER_TYPE;
    int REAL_TYPE;
    int STRING_TYPE;
    int TIMESTAMP_TYPE;
    int NULL_TYPE;

public:
    LogRecordset(ILogRecordset* rs)
        : lrs(rs)
    {
        int retval = 0;
        Marshal::ThrowExceptionForHR(lrs->get_INTEGER_TYPE(&retval));
        INTEGER_TYPE = retval;
        Marshal::ThrowExceptionForHR(lrs->get_REAL_TYPE(&retval));
        REAL_TYPE = retval;
        Marshal::ThrowExceptionForHR(lrs->get_STRING_TYPE(&retval));
        STRING_TYPE = retval;
        Marshal::ThrowExceptionForHR(lrs->get_TIMESTAMP_TYPE(&retval));
        TIMESTAMP_TYPE = retval;
        Marshal::ThrowExceptionForHR(lrs->get_NULL_TYPE(&retval));
        NULL_TYPE = retval;
    }

    property int ColumnCount
    {
        int get()
        {
            int retval = 0;
            Marshal::ThrowExceptionForHR(lrs->getColumnCount(&retval));
            return retval;
        };
    }

    property bool atEnd
    {
        bool get()
        {
            VARIANT_BOOL vb;
            Marshal::ThrowExceptionForHR(lrs->atEnd(&vb));
            return vb == 0 ? false : true;
        }
    }

    System::String^ getColumnName(int Ordinal) 
		{
			BSTR name = NULL;
			Marshal::ThrowExceptionForHR(lrs->getColumnName(Ordinal, &name));
			System::String ^retval = marshal_as<System::String^>(name);
			Marshal::FreeBSTR((IntPtr)name);
			return retval;
		}

		System::Type^ getColumnManagedType(int Ordinal)
		{
			int  type = 0;
			Marshal::ThrowExceptionForHR(lrs->getColumnType(Ordinal, &type));
			if(type == INTEGER_TYPE) 
				return System::Int32::typeid;
			else if(type == REAL_TYPE)
				return System::Double::typeid;
			else if(type == STRING_TYPE)
				return System::String::typeid;
			else if(type == TIMESTAMP_TYPE)
				return System::Data::SqlTypes::SqlDateTime::typeid;
			else if(type == NULL_TYPE)
				return System::Object::typeid;
			else
				throw gcnew InvalidCastException();
		};

    int getColumnLogParserType(int Ordinal)
    {
        int type = 0;
        Marshal::ThrowExceptionForHR(lrs->getColumnType(Ordinal, &type));
        return type;
    }

    property LogRecord
        ^ Record { LogRecord ^ get() { return m_Current; } }

        void
        moveNext()
    {
        Marshal::ThrowExceptionForHR(lrs->moveNext());
        if (atEnd)
            m_Current = nullptr;
        else
        {
            ILogRecord* ptr = nullptr;
            Marshal::ThrowExceptionForHR(lrs->getRecord(&ptr));
            m_Current = gcnew LogRecord(ptr);
        }
    }

    ~LogRecordset() { this->!LogRecordset(); }
    !LogRecordset()
    {
        lrs->close();
        lrs.Release();
    }

private:
    com::ptr<ILogRecordset> lrs;
    LogRecord ^ m_Current;
};

LogParserDataReader::LogParserDataReader(void)
{
    m_Query = gcnew TaskUtils::LogQuery();
}

void LogParserDataReader::DeduceSchema()
{

    // SchemaTable
    m_Schema = gcnew DataTable("Table");

    m_Schema->Columns->Add(SchemaTableColumn::ColumnName, System::String::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::ColumnSize, System::Int32::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::ColumnOrdinal, System::Int32::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::NumericPrecision, System::Int16::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::NumericScale, System::Int16::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::DataType, System::Type::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::AllowDBNull, System::Boolean::typeid);
    m_Schema->Columns->Add(SchemaTableOptionalColumn::IsReadOnly, System::Boolean::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::IsUnique, System::Boolean::typeid);
    m_Schema->Columns->Add(SchemaTableOptionalColumn::IsRowVersion, System::Boolean::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::IsKey, System::Boolean::typeid);
    m_Schema->Columns->Add(SchemaTableOptionalColumn::IsAutoIncrement, System::Boolean::typeid);
    m_Schema->Columns->Add(SchemaTableColumn::IsLong, System::Boolean::typeid);

    int columnCount = m_RecordSet->ColumnCount;

    m_columnLPTypes = gcnew array<int>(columnCount);
    m_columnNames = gcnew array<String ^>(columnCount);
    m_columnOrdinals = gcnew System::Collections::Generic::Dictionary<String ^, int>(columnCount);
    m_columnTypes = gcnew array<Type ^>(columnCount);

    for (int i = 0; i < columnCount; i++)
    {
        m_columnLPTypes[i] = m_RecordSet->getColumnLogParserType(i);
        m_columnTypes[i] = m_RecordSet->getColumnManagedType(i);
        m_columnNames[i] = m_RecordSet->getColumnName(i);
        m_columnOrdinals->Add(m_columnNames[i], i);

        DataRow ^ row = m_Schema->NewRow();

        row[SchemaTableColumn::ColumnName] = m_columnNames[i];
        row[SchemaTableColumn::ColumnSize] = -1;
        row[SchemaTableColumn::ColumnOrdinal] = i;
        row[SchemaTableColumn::NumericPrecision] = (Int32 ^)0;
        row[SchemaTableColumn::NumericScale] = (Int32 ^)0;
        row[SchemaTableColumn::DataType] = m_columnTypes[i];
        row[SchemaTableColumn::AllowDBNull] = true;
        row[SchemaTableOptionalColumn::IsReadOnly] = true;
        row[SchemaTableColumn::IsUnique] = false;
        row[SchemaTableOptionalColumn::IsRowVersion] = false;
        row[SchemaTableColumn::IsKey] = false;
        row[SchemaTableOptionalColumn::IsAutoIncrement] = false;
        row[SchemaTableColumn::IsLong] = false;

        m_Schema->Rows->Add(row);
    }
}

IEnumerator ^ LogParserDataReader::GetEnumerator()
{
    return ((IEnumerator ^) gcnew DbEnumerator(this));
}

// IDataReader

void LogParserDataReader::Close(void) {}

System::Data::DataTable ^ LogParserDataReader::GetSchemaTable(void)
{
    return m_Schema;
}

bool LogParserDataReader::NextResult(void)
{
    return false;
}

bool LogParserDataReader::Read(void)
{
    return false;
}

int LogParserDataReader::Depth::get(void)
{
    return 0;
}

bool LogParserDataReader::IsClosed::get(void)
{

    return false;
}

int LogParserDataReader::RecordsAffected::get(void)
{
    return -1;
}

// IDataRecord

System::Object ^ LogParserDataReader::Item::get(int Ordinal)
{
    return GetValue(Ordinal);
}

System::Object ^ LogParserDataReader::Item::get(System::String ^ name)
{
    return GetValue(GetOrdinal(name));
}

System::String ^ LogParserDataReader::GetName(int Ordinal)
{
    // m_RecordSet
    return L"";
}

System::String ^ LogParserDataReader::GetDataTypeName(int Ordinal)
{
    return L"";
}

System::Type ^ LogParserDataReader::GetFieldType(int Ordinal)
{
    throw gcnew InvalidCastException();
}

System::Object ^ LogParserDataReader::GetValue(int Ordinal)
{
    return L"";
}

int LogParserDataReader::GetValues(cli::array<System::Object ^, 1> ^ theArray)
{
    return 0;
}

int LogParserDataReader::GetOrdinal(System::String ^ name)
{

    throw gcnew IndexOutOfRangeException();
}

bool LogParserDataReader::GetBoolean(int Ordinal)
{
    throw gcnew InvalidCastException();
}

unsigned char LogParserDataReader::GetByte(int Ordinal)
{
    throw gcnew InvalidCastException();
}

__int64 LogParserDataReader::GetBytes(int, __int64, cli::array<unsigned char, 1> ^, int, int)
{
    throw gcnew InvalidCastException();
}

wchar_t LogParserDataReader::GetChar(int Ordinal)
{
    throw gcnew InvalidCastException();
}

__int64 LogParserDataReader::GetChars(int, __int64, cli::array<wchar_t, 1> ^, int, int)
{
    throw gcnew InvalidCastException();
}

System::Guid LogParserDataReader::GetGuid(int)
{
    throw gcnew InvalidCastException();
}

short LogParserDataReader::GetInt16(int Ordinal)
{
    throw gcnew InvalidCastException();
}

int LogParserDataReader::GetInt32(int Ordinal)
{
    throw gcnew InvalidCastException();
}

__int64 LogParserDataReader::GetInt64(int Ordinal)
{
    throw gcnew InvalidCastException();
}

float LogParserDataReader::GetFloat(int Ordinal)
{
    throw gcnew InvalidCastException();
}

double LogParserDataReader::GetDouble(int Ordinal)
{
    throw gcnew InvalidCastException();
}

System::String ^ LogParserDataReader::GetString(int Ordinal)
{
    throw gcnew InvalidCastException();
}

System::Decimal LogParserDataReader::GetDecimal(int Ordinal)
{
    throw gcnew InvalidCastException();
}

System::DateTime LogParserDataReader::GetDateTime(int Ordinal)
{
    throw gcnew InvalidCastException();
}

System::Data::IDataReader ^ LogParserDataReader::GetData(int)
{
    throw gcnew System::Data::DataException();
}

bool LogParserDataReader::IsDBNull(int)
{
    return false;
}

LogParserDataReader::~LogParserDataReader(void) {}

};  // namespace TaskUtils
