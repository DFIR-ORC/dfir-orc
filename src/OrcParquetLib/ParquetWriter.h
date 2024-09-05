//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ByteStream.h"

#include "TableOutputWriter.h"
#include "OutputSpec.h"
#include "CriticalSection.h"

#include "Convert.h"

#include <variant>

#include "ParquetDefinitions.h"

#include <limits>

namespace parquet {
class WriterProperties;
namespace arrow {
class FileWriter;
}
}  // namespace parquet

namespace arrow {
class Schema;
class DataType;
class ArrayBuilder;
}  // namespace arrow

namespace Orc::Test::Parquet {
class ParquetWriter;
}

namespace Orc::TableOutput::Parquet {

constexpr auto WRITE_BUFFER = (0x100000);

class WriterTermination;

class Writer
    : public TableOutput::Writer
    , public TableOutput::IStreamWriter
{
    friend class Orc::Test::Parquet::ParquetWriter;

public:
    static std::shared_ptr<Writer> MakeNew(std::unique_ptr<Options>&& options);

    Writer(const Writer&) = delete;
    Writer(Writer&& other) noexcept
    {
        std::swap(m_pTermination, other.m_pTermination);
        wcscpy_s(m_szFileName, other.m_szFileName);
    }

    std::shared_ptr<ByteStream> GetStream() const override final { return m_pByteStream; };

    STDMETHOD(WriteToFile)(const std::filesystem::path& path) override final;
    STDMETHOD(WriteToFile)(const WCHAR* szFileName) override final;
    STDMETHOD(WriteToStream)(const std::shared_ptr<ByteStream>& pStream, bool bCloseStream = true) override final;

    ITableOutput& GetTableOutput() { return static_cast<ITableOutput&>(*this); }

    STDMETHOD(SetSchema)(const TableOutput::Schema& columns) override final;
    ;

    virtual DWORD GetCurrentColumnID() override final { return m_dwColumnCounter; };

    virtual const TableOutput::Column& GetCurrentColumn() override final
    {
        if (m_Schema)
            return m_Schema[m_dwColumnCounter];
        else
            throw L"No schema assicated with Parquet writer";
    }

    STDMETHOD(Flush)() override final;
    STDMETHOD(Close)() override final;

    STDMETHOD(WriteNothing)() override final;

    STDMETHOD(WriteString)(const std::string& szString) override final;
    STDMETHOD(WriteString)(const std::string_view& szString) override final;
    STDMETHOD(WriteString)(const CHAR* szString) override final;
    STDMETHOD(WriteCharArray)(const CHAR* szArray, DWORD dwCharCount) override final;

    STDMETHOD(WriteString)(const std::wstring& szString) override final;
    STDMETHOD(WriteString)(const std::wstring_view& szString) override final;
    STDMETHOD(WriteString)(const WCHAR* szString) override final;
    STDMETHOD(WriteCharArray)(const WCHAR* szArray, DWORD dwCharCount) override final;

protected:
    STDMETHOD(WriteFormated_)(const std::string_view& szFormat, fmt::format_args args) override final;
    STDMETHOD(WriteFormated_)(const std::wstring_view& szFormat, fmt::wformat_args args) override final;

public:
    STDMETHOD(WriteAttributes)(DWORD dwAttibutes) override final;
    STDMETHOD(WriteFileTime)(FILETIME fileTime) override final;
    STDMETHOD(WriteFileTime)(LONGLONG fileTime) override final;
    STDMETHOD(WriteTimeStamp)(time_t tmStamp) override final;
    STDMETHOD(WriteTimeStamp)(tm tmStamp) override final;

    STDMETHOD(WriteFileSize)(LARGE_INTEGER fileSize) override final;
    STDMETHOD(WriteFileSize)(ULONGLONG fileSize) override final;
    STDMETHOD(WriteFileSize)(DWORD nFileSizeHigh, DWORD nFileSizeLow) override final;

    STDMETHOD(WriteInteger)(DWORD dwInteger) override final;
    STDMETHOD(WriteInteger)(LONGLONG dw64Integer) override final;
    STDMETHOD(WriteInteger)(ULONGLONG dw64Integer) override final;

    STDMETHOD(WriteBytes)(const BYTE pSHA1[], DWORD dwLen) override final;
    STDMETHOD(WriteBytes)(const CBinaryBuffer& Buffer) override final;

    STDMETHOD(WriteBool)(bool bBoolean) override final;

    STDMETHOD(WriteEnum)(DWORD dwEnum) override final;
    STDMETHOD(WriteEnum)(DWORD dwEnum, const WCHAR* EnumValues[]) override final;
    STDMETHOD(WriteFlags)(DWORD dwFlags) override final;
    STDMETHOD(WriteFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final;
    STDMETHOD(WriteExactFlags)(DWORD dwFlags) override final;
    STDMETHOD(WriteExactFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[]) override final;

    STDMETHOD(WriteGUID)(const GUID& guid) override final;

    STDMETHOD(WriteXML)(const WCHAR* szString) override final;
    STDMETHOD(WriteXML)(const CHAR* szString) override final;
    STDMETHOD(WriteXML)(const WCHAR* szArray, DWORD dwCharCount) override final;
    STDMETHOD(WriteXML)(const CHAR* szArray, DWORD dwCharCount) override final;

    STDMETHOD(AbandonRow)() override final;
    STDMETHOD(AbandonColumn)() override final;

    virtual HRESULT WriteEndOfLine() override final;

    virtual ~Writer();

protected:
    Writer(std::unique_ptr<Options>&& options);

    std::unique_ptr<Options> m_Options;
    std::shared_ptr<WriterTermination> m_pTermination;

    WCHAR m_szFileName[MAX_PATH] = {0};

    CriticalSection m_cs;

    DWORD m_dwColumnCounter = 0L;
    DWORD m_dwColumnNumber = 0L;

    DWORD m_dwBatchRowCount = 0L;
    DWORD m_dwTotalRowCount = 0L;

    // Parquet specifics
    std::shared_ptr<parquet::WriterProperties> m_parquetProps;
    std::shared_ptr<arrow::Schema> m_arrowSchema;

    std::shared_ptr<ByteStream> m_pByteStream = nullptr;
    bool m_bCloseStream = true;
    std::unique_ptr<parquet::arrow::FileWriter> m_arrowWriter;

    using ColumnBuilder = std::variant<
        std::unique_ptr<arrow::NullBuilder>,
        std::unique_ptr<arrow::UInt8Builder>,
        std::unique_ptr<arrow::Int8Builder>,
        std::unique_ptr<arrow::Int16Builder>,
        std::unique_ptr<arrow::UInt16Builder>,
        std::unique_ptr<arrow::Int32Builder>,
        std::unique_ptr<arrow::UInt32Builder>,
        std::unique_ptr<arrow::Int64Builder>,
        std::unique_ptr<arrow::UInt64Builder>,
        std::unique_ptr<arrow::TimestampBuilder>,
        std::unique_ptr<arrow::BooleanBuilder>,
        std::unique_ptr<arrow::StringBuilder>,
        std::unique_ptr<arrow::SparseUnionBuilder>,
        std::unique_ptr<arrow::BinaryBuilder>,
        std::unique_ptr<arrow::FixedSizeBinaryBuilder>,
        std::unique_ptr<arrow::StringDictionaryBuilder>,
        std::unique_ptr<arrow::StructBuilder>,
        std::unique_ptr<arrow::ArrayBuilder>>;

    using Builders = std::vector<ColumnBuilder>;

    Builders m_arrowBuilders;

    Builders GetBuilders();

    HRESULT AddColumnAndCheckNumbers();

    template <arrow::TimeUnit::type timeUnit = arrow::TimeUnit::MICRO>
    static LONGLONG ConvertTo(FILETIME fileTime)
    {
        LARGE_INTEGER date, adjust;
        date.HighPart = fileTime.dwHighDateTime;
        date.LowPart = fileTime.dwLowDateTime;

        // 100-nanoseconds = milliseconds * 10000
        adjust.QuadPart = 11644473600000 * 10000;
        // removes the diff between 1970 and 1601
        date.QuadPart -= adjust.QuadPart;

        // converts back from 100-nanoseconds to seconds
        if constexpr (timeUnit == arrow::TimeUnit::NANO)
            return date.QuadPart * 100;
        else if constexpr (timeUnit == arrow::TimeUnit::MICRO)
            return date.QuadPart / 10;
    }

    template <arrow::TimeUnit::type timeUnit = arrow::TimeUnit::MICRO>
    static LONGLONG ConvertTo(time_t timeStamp)
    {
        return 0LL;
    }

    template <arrow::TimeUnit::type timeUnit = arrow::TimeUnit::MICRO>
    static LONGLONG ConvertTo(tm timeStamp)
    {
        return 0LL;
    }
};
}  // namespace Orc::TableOutput::Parquet
