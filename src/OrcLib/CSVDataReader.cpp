//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "CSVDataReader.h"

#include <vcclr.h>

using namespace System::Data::Common;
using namespace TaskUtils;

#include "CSVFileReader.h"

CSVDataReader::CSVDataReader()
{
    m_pW = new LogFileWriter();
    m_pReader = new CSVFileReader(m_pW);
    m_pCurrentRecord = NULL;

    System::Data::SqlTypes::SqlDateTime min = System::Data::SqlTypes::SqlDateTime::MinValue;
    m_minvalue = min.Value;
    System::Data::SqlTypes::SqlDateTime max = System::Data::SqlTypes::SqlDateTime::MaxValue;
    m_maxvalue = max.Value;
}

void CSVDataReader::OpenFile(System::String ^ pFileName)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;

    pin_ptr<const wchar_t> str = PtrToStringChars(pFileName);

    if (FAILED(hr = m_pReader->OpenFile(str)))
        Marshal::ThrowExceptionForHR(hr);
}

void CSVDataReader::OpenFile(
    System::String ^ FileName,
    System::Boolean bfirstRowIsColumnNames,
    System::Char wcSeparator,
    System::Char wcQuote,
    System::String ^ DateFormat,
    System::Int32 dwSkipLines)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;

    pin_ptr<const wchar_t> szFileName = PtrToStringChars(FileName);
    pin_ptr<const wchar_t> szDateFormat = PtrToStringChars(DateFormat);

    if (FAILED(
            hr = m_pReader->OpenFile(
                szFileName, bfirstRowIsColumnNames, wcSeparator, wcQuote, szDateFormat, dwSkipLines)))
        Marshal::ThrowExceptionForHR(hr);
}

void CSVDataReader::DeduceSchema()
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;

    System::String ^ FileName = Marshal::PtrToStringUni((IntPtr)(wchar_t*)m_pReader->GetFileName());

    m_Schema = gcnew DataTable(FileName);

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

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    int columnCount = schema.dwNbColumns;

    for (int i = 0; i < columnCount; i++)
    {
        const CSVColumnDef& coldef = schema.Column[i];

        System::Data::DataRow ^ row = m_Schema->NewRow();

        row[SchemaTableColumn::ColumnName] = Marshal::PtrToStringUni((IntPtr)(wchar_t*)coldef.Name);
        row[SchemaTableColumn::ColumnSize] = -1;
        row[SchemaTableColumn::ColumnOrdinal] = (System::Int32 ^) i;
        row[SchemaTableColumn::NumericPrecision] = (System::Int32 ^)0;
        row[SchemaTableColumn::NumericScale] = (System::Int32 ^)0;

        switch (coldef.Type)  // UnknownType = 0, String, Integer, LargeInteger, DateTime,
        {
            case ::String:
                row[SchemaTableColumn::DataType] = System::String::typeid;
                break;
            case ::Integer:
                row[SchemaTableColumn::DataType] = System::Int32::typeid;
                break;
            case ::LargeInteger:
                row[SchemaTableColumn::DataType] = System::Int64::typeid;
                break;
            case ::DateTime:
                row[SchemaTableColumn::DataType] = System::Data::SqlTypes::SqlDateTime::typeid;
                break;
            default:
                row[SchemaTableColumn::DataType] = System::Object::typeid;
                break;
        }

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

void CSVDataReader::PeekHeaders()
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_pReader->PeekHeaders()))
        Marshal::ThrowExceptionForHR(hr);
}

void CSVDataReader::PeekHeaders(System::String ^ HeadersFileName)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;
    pin_ptr<const wchar_t> szHeadersFileName = PtrToStringChars(HeadersFileName);

    if (FAILED(hr = m_pReader->PeekHeaders(szHeadersFileName)))
        Marshal::ThrowExceptionForHR(hr);
}

void CSVDataReader::PeekTypes()
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_pReader->PeekTypes()))
        Marshal::ThrowExceptionForHR(hr);

    DeduceSchema();
}

void CSVDataReader::PeekTypes(System::Int32 cbLinesToPeek)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    HRESULT hr = E_FAIL;
    if (FAILED(hr = m_pReader->PeekTypes(cbLinesToPeek)))
        Marshal::ThrowExceptionForHR(hr);

    DeduceSchema();
}

IEnumerator ^ CSVDataReader::GetEnumerator()
{
    return ((IEnumerator ^) gcnew DbEnumerator(this));
}

// IDataReader

void CSVDataReader::Close(void) {}

System::Data::DataTable ^ CSVDataReader::GetSchemaTable(void)
{
    return m_Schema;
}

bool CSVDataReader::NextResult(void)
{
    return false;
}

bool CSVDataReader::Read(void)
{
    if (m_pCurrentRecord == NULL)
    {
        m_pCurrentRecord = new CSVRecord();
    }
    ZeroMemory(m_pCurrentRecord, sizeof(CSVRecord));

    HRESULT hr = m_pReader->ParseNextLine(*m_pCurrentRecord);

    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
        Marshal::ThrowExceptionForHR(hr);

    return hr == HRESULT_FROM_WIN32(ERROR_HANDLE_EOF) ? false : true;
}

int CSVDataReader::Depth::get(void)
{
    return 0;
}

bool CSVDataReader::IsClosed::get(void)
{
    if (m_pReader == NULL)
        return true;
    return false;
}

int CSVDataReader::RecordsAffected::get(void)
{
    return -1;
}

// IDataRecord

System::Object ^ CSVDataReader::Item::get(int Ordinal)
{
    return GetValue(Ordinal);
}

System::Object ^ CSVDataReader::Item::get(System::String ^ name)
{
    return GetValue(GetOrdinal(name));
}

System::String ^ CSVDataReader::GetName(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    System::String ^ ColumnName = Marshal::PtrToStringUni((IntPtr)(wchar_t*)schema.Column[Ordinal].Name);

    return ColumnName;
}

System::String ^ CSVDataReader::GetDataTypeName(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return L"String";
        case ::Integer:
            return L"Integer";
        case ::LargeInteger:
            return L"LargeInteger";
        case ::DateTime:
            return L"DateTime";
        default:
            return L"Unknown";
    }
}

System::Type ^ CSVDataReader::GetFieldType(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::String::typeid;
        case ::Integer:
            return System::Int32::typeid;
        case ::LargeInteger:
            return System::Int64::typeid;
        case ::DateTime:
            return System::Data::SqlTypes::SqlDateTime::typeid;
        default:
            return System::Object::typeid;
    }
}

System::Object ^ CSVDataReader::GetValue(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength);
        case ::Integer:
            return m_pCurrentRecord->Values[Ordinal].dwInteger;
        case ::LargeInteger:
            return m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart;
        case ::DateTime: {
            LARGE_INTEGER ft;
            ft.LowPart = m_pCurrentRecord->Values[Ordinal].ftDateTime.dwLowDateTime;
            ft.HighPart = m_pCurrentRecord->Values[Ordinal].ftDateTime.dwHighDateTime;

            System::DateTime aDate = System::DateTime::FromFileTimeUtc(ft.QuadPart);

            if (aDate.Year <= m_minvalue.Year)
                return System::Data::SqlTypes::SqlDateTime::MinValue;
            else if (aDate >= m_maxvalue)
                return System::Data::SqlTypes::SqlDateTime::MaxValue;
            else
                return gcnew System::Data::SqlTypes::SqlDateTime(aDate);
        }
        default:
            return nullptr;
    }
}

int CSVDataReader::GetValues(cli::array<System::Object ^, 1> ^ theArray)
{
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    array<System::Object ^, 1> ^ local = theArray;

    for (unsigned int i = 0; i < schema.dwNbColumns; i++)
    {
        // UnknownType = 0, String, Integer, LargeInteger, DateTime,
        switch (schema.Column[i].Type)
        {
            case ::String:
                local[i] = Marshal::PtrToStringUni(
                    (IntPtr)(wchar_t*)m_pCurrentRecord->Values[i].String.szString,
                    m_pCurrentRecord->Values[i].String.dwLength);
                break;
            case ::Integer:
                local[i] = m_pCurrentRecord->Values[i].dwInteger;
                break;
            case ::LargeInteger:
                local[i] = m_pCurrentRecord->Values[i].liLargeInteger.QuadPart;
                break;
            case ::DateTime: {
                LARGE_INTEGER ft;
                ft.LowPart = m_pCurrentRecord->Values[i].ftDateTime.dwLowDateTime;
                ft.HighPart = m_pCurrentRecord->Values[i].ftDateTime.dwHighDateTime;

                System::DateTime aDate = System::DateTime::FromFileTimeUtc(ft.QuadPart);

                if (aDate.Year <= m_minvalue.Year)
                    local[i] = System::Data::SqlTypes::SqlDateTime::MinValue;
                else if (aDate >= m_maxvalue)
                    local[i] = System::Data::SqlTypes::SqlDateTime::MaxValue;
                else
                    local[i] = gcnew System::Data::SqlTypes::SqlDateTime(aDate);
            }
            break;
            default:
                local[i] = nullptr;
                break;
        }
    }
    theArray = local;
    return schema.dwNbColumns;
}

int CSVDataReader::GetOrdinal(System::String ^ name)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    for (unsigned int i = 0; i < schema.dwNbColumns; i++)
    {
        pin_ptr<const wchar_t> pinchars = PtrToStringChars(name);

        if (!wcsncmp(pinchars, schema.Column[i].Name, name->Length))
        {
            return schema.Column[i].Index;
        }
    }
    for (unsigned int i = 0; i < schema.dwNbColumns; i++)
    {
        pin_ptr<const wchar_t> pinchars = PtrToStringChars(name);

        if (!_wcsnicmp(pinchars, schema.Column[i].Name, name->Length))
        {
            return schema.Column[i].Index;
        }
    }

    throw gcnew IndexOutOfRangeException();
}

bool CSVDataReader::GetBoolean(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToBoolean(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToBoolean((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToBoolean((__int64)m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

unsigned char CSVDataReader::GetByte(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToByte(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToByte((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToByte(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

__int64 CSVDataReader::GetBytes(int, __int64, cli::array<unsigned char, 1> ^, int, int)
{
    throw gcnew InvalidCastException();
}

wchar_t CSVDataReader::GetChar(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToChar(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToChar((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToChar(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

__int64 CSVDataReader::GetChars(int, __int64, cli::array<wchar_t, 1> ^, int, int)
{
    throw gcnew InvalidCastException();
}

System::Guid CSVDataReader::GetGuid(int)
{
    throw gcnew InvalidCastException();
}

short CSVDataReader::GetInt16(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToInt16(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToInt16((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToInt16(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

int CSVDataReader::GetInt32(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToInt32(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToInt32((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToInt32(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

__int64 CSVDataReader::GetInt64(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToInt64(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToInt64((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToInt64(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

float CSVDataReader::GetFloat(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToSingle(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                (int)m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToSingle((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToSingle(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

double CSVDataReader::GetDouble(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToDouble(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToDouble((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToDouble(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

System::String ^ CSVDataReader::GetString(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength);
        case ::Integer:
            return System::Convert::ToString((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToString(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime: {
            LARGE_INTEGER ft;
            ft.LowPart = m_pCurrentRecord->Values[Ordinal].ftDateTime.dwLowDateTime;
            ft.HighPart = m_pCurrentRecord->Values[Ordinal].ftDateTime.dwHighDateTime;
            return System::DateTime::FromFileTimeUtc(ft.QuadPart).ToString();
        }
        default:
            throw gcnew InvalidCastException();
    }
}

System::Decimal CSVDataReader::GetDecimal(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToDecimal(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToDecimal((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToDecimal(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime:
            throw gcnew InvalidCastException();
        default:
            throw gcnew InvalidCastException();
    }
}

System::DateTime CSVDataReader::GetDateTime(int Ordinal)
{
    if (m_pReader == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);
    if (m_pCurrentRecord == NULL)
        Marshal::ThrowExceptionForHR(E_POINTER);

    const CSVRecordSchema& schema = m_pReader->GetSchema();

    if (Ordinal >= (int)schema.dwNbColumns)
        throw gcnew IndexOutOfRangeException();

    // UnknownType = 0, String, Integer, LargeInteger, DateTime,
    switch (schema.Column[Ordinal].Type)
    {
        case ::String:
            return System::Convert::ToDateTime(Marshal::PtrToStringUni(
                (IntPtr)(wchar_t*)m_pCurrentRecord->Values[Ordinal].String.szString,
                m_pCurrentRecord->Values[Ordinal].String.dwLength));
        case ::Integer:
            return System::Convert::ToDateTime((int)m_pCurrentRecord->Values[Ordinal].dwInteger);
        case ::LargeInteger:
            return System::Convert::ToDateTime(m_pCurrentRecord->Values[Ordinal].liLargeInteger.QuadPart);
        case ::DateTime: {
            LARGE_INTEGER ft;
            ft.LowPart = m_pCurrentRecord->Values[Ordinal].ftDateTime.dwLowDateTime;
            ft.HighPart = m_pCurrentRecord->Values[Ordinal].ftDateTime.dwHighDateTime;

            System::DateTime aDate = System::DateTime::FromFileTimeUtc(ft.QuadPart);

            if (aDate.Year <= m_minvalue.Year)
                return System::Data::SqlTypes::SqlDateTime::MinValue.Value;
            else if (aDate >= m_maxvalue)
                return System::Data::SqlTypes::SqlDateTime::MaxValue.Value;
            else
                return System::Data::SqlTypes::SqlDateTime(aDate).Value;
        }
        default:
            throw gcnew InvalidCastException();
    }
}

System::Data::IDataReader ^ CSVDataReader::GetData(int)
{
    throw gcnew System::Data::DataException();
}

bool CSVDataReader::IsDBNull(int)
{
    return false;
}
