//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"

#include "ParquetStream.h"
#include "ParquetOutputWriter.h"
#include "TableOutputWriter.h"

#include "ParameterCheck.h"
#include "Temporary.h"
#include "FileStream.h"

#include "Buffer.h"
#include "WideAnsi.h"

#undef OPTIONAL

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <arrow/array.h>
#include <parquet/exception.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace std::string_literals;
using namespace Orc;
using namespace Orc::Test;
using namespace Orc::TableOutput::Parquet;

namespace Orc::Test::Parquet {
TEST_CLASS(ParquetOutput)
{
private:
    logger _L_;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
        _L_ = std::make_shared<LogFileWriter>();
        helper.InitLogFileWriter(_L_);
    }
    TEST_METHOD_CLEANUP(Finalize) { helper.FinalizeLogFileWriter(_L_); }
    TEST_METHOD(LoadParquetExtension)
    {
        auto extension = Orc::ExtensionLibrary::GetLibrary<ParquetOutputWriter>(_L_);
        Assert::IsTrue((bool)extension, L"Failed to load ParquetOutputWriter extension");

        auto output_writer = extension->StreamTableFactory(_L_, nullptr);
        Assert::IsTrue((bool)output_writer, L"Failed to create parquet output writer instance");
    }
    TEST_METHOD(ParquetStream)
    {
        using namespace std::string_literals;
        auto strText = L"Hello World!!"s;
        auto strFileName = L"%TEMP%\\test.parquet"s;

        auto strPath = GetFilePath(strFileName);

        auto dwTextLen = strText.size() * sizeof(WCHAR);
        {
            auto stream = std::make_shared<FileStream>(_L_);
            Assert::IsTrue(SUCCEEDED(stream->WriteTo(strPath.c_str())));
            auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
            Assert::IsTrue(SUCCEEDED(arrow_stream->Open(stream)), L"Failed to open arrow stream");

            auto arrow_output_stream = std::make_shared<parquet::ArrowOutputStream>(arrow_stream);
            arrow_output_stream->Write((LPBYTE)strText.data(), dwTextLen);
            arrow_output_stream->Close();
        }
        {
            auto stream = std::make_shared<FileStream>(_L_);
            stream->ReadFrom(strPath.c_str());
            auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
            arrow_stream->Open(stream);

            INT64 size = 0LL;
            auto status = arrow_stream->GetSize(&size);
            Assert::IsTrue(status.ok());

            ByteBuffer buffer;
            buffer.reserve(static_cast<ULONG>(size));

            INT64 bytesRead = 0LL;
            status = arrow_stream->Read(size, &bytesRead, buffer.get());
            Assert::IsTrue(status.ok());

            Assert::IsTrue(memcmp((LPBYTE)strText.data(), (LPBYTE)buffer, dwTextLen) == 0);

            std::shared_ptr<arrow::Buffer> arrow_buffer;
            status = arrow_stream->ReadAt(0LL, dwTextLen, &arrow_buffer);
            Assert::IsTrue(status.ok());

            Assert::IsTrue(memcmp((LPBYTE)strText.data(), (LPBYTE)arrow_buffer->data(), dwTextLen) == 0);
        }
        UtilDeleteTemporaryFile(_L_, strPath.c_str());
    }
    // #0 Build dummy data to pass around
    // To have some input data, we first create an Arrow Table that holds
    // some data.
    std::shared_ptr<arrow::Table> generate_table(
        const std::shared_ptr<arrow::Schema>& schema, int table_no, int row_number)
    {

        arrow::Int32Builder sip_builder(arrow::default_memory_pool());
        arrow::Int32Builder dip_builder(arrow::default_memory_pool());
        arrow::Int32Builder table_builder(arrow::default_memory_pool());
        arrow::StringBuilder code_builder(arrow::default_memory_pool());

        std::shared_ptr<arrow::Table> arrow_table;

        for (size_t i = 0; i < row_number; i++)
        {
            sip_builder.Append(static_cast<const int>(i * 100));
            dip_builder.Append(static_cast<const int>(i * 10 + i));
            table_builder.Append(static_cast<const int>(table_no));
            code_builder.Append("Testing["s + std::to_string(i) + "]");
        }

        std::shared_ptr<arrow::Array> sip_array;
        sip_builder.Finish(&sip_array);

        std::shared_ptr<arrow::Array> dip_array;
        dip_builder.Finish(&dip_array);

        std::shared_ptr<arrow::Array> table_array;
        table_builder.Finish(&table_array);

        std::shared_ptr<arrow::Array> code_array;
        code_builder.Finish(&code_array);

        return arrow::Table::Make(schema, {sip_array, dip_array, table_array, code_array});
    }

    // #2: Fully read in the file
    void read_whole_file(LPCWSTR szFileName)
    {
        auto file_stream = std::make_shared<FileStream>(_L_);

        file_stream->ReadFrom(szFileName);
        auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
        arrow_stream->Open(file_stream);

        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(arrow_stream, arrow::default_memory_pool(), &reader));

        std::shared_ptr<arrow::Table> table;

        for (int i = 0; i < 1; i++)
        {
            PARQUET_THROW_NOT_OK(reader->ReadTable(&table));
            log::Info(_L_, L"Loaded %I64d rows in %d columns\r\n", table->num_rows(), table->num_columns());
        }
    }

    // #3: Read only a single RowGroup of the parquet file
    void read_single_rowgroup(LPCWSTR szFileName)
    {
        auto file_stream = std::make_shared<FileStream>(_L_);

        file_stream->ReadFrom(szFileName);
        auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
        arrow_stream->Open(file_stream);

        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(arrow_stream, arrow::default_memory_pool(), &reader));
        std::shared_ptr<arrow::Table> table;
        PARQUET_THROW_NOT_OK(reader->RowGroup(0)->ReadTable(&table));
        log::Info(_L_, L"Loaded %I64d rows in %d columns\r\n", table->num_rows(), table->num_columns());
    }

    // #4: Read only a single column of the whole parquet file
    void read_single_column(LPCWSTR szFileName, int column)
    {
        log::Info(_L_, L"Reading first column of %s\r\n", szFileName);
        auto file_stream = std::make_shared<FileStream>(_L_);

        file_stream->ReadFrom(szFileName);
        auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
        arrow_stream->Open(file_stream);

        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(arrow_stream, arrow::default_memory_pool(), &reader));

        std::shared_ptr<arrow::ChunkedArray> array;
        PARQUET_THROW_NOT_OK(reader->ReadColumn(column, &array));

        auto type = array->type();

        log::Info(_L_, L"Column type %S\r\n", type->name().c_str());
        log::Info(_L_, L"Column chunks %d\r\n", array->num_chunks());
        log::Info(_L_, L"Column length %I64d\r\n", array->length());

        INT64 total = 0LL;

        for (auto chunk_idx = 0; chunk_idx < array->num_chunks(); chunk_idx++)
        {
            const auto chunk_ptr = array->chunk(chunk_idx);
            if (!chunk_ptr)
                continue;
            if (auto int32_array = std::dynamic_pointer_cast<arrow::Int32Array>(chunk_ptr))
            {
                for (unsigned int i = 0; i < int32_array->length(); i++)
                {
                    total += int32_array->Value(i);
                }
            }
            else if (auto string_array = std::dynamic_pointer_cast<arrow::StringArray>(chunk_ptr))
            {
                for (unsigned int i = 0; i < string_array->length(); i++)
                {
                    int32_t nchars = 0;
                    const uint8_t* str = string_array->GetValue(i, &nchars);
                    log::Info(_L_, L"array[%i] = %S\r\n", i, (const char*)str);
                }
            }
        }
        log::Info(_L_, L"total of array = %I64d\r\n", total);
    }

    // #5: Read only a single column of a RowGroup (this is known as ColumnChunk)
    //     from the Parquet file.
    void read_single_column_chunk(LPCSTR szFileName)
    {
        // console->info("Reading first ColumnChunk of the first RowGroup of parquet-arrow-example.parquet");
        std::shared_ptr<arrow::io::ReadableFile> infile;
        PARQUET_THROW_NOT_OK(arrow::io::ReadableFile::Open(szFileName, arrow::default_memory_pool(), &infile));

        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader));
        std::shared_ptr<arrow::ChunkedArray> array;
        PARQUET_THROW_NOT_OK(reader->RowGroup(0)->Column(0)->Read(&array));
        // console->info("Read {} elements", array->length());
    }
    TEST_METHOD(TableReadWrite)
    {
        using namespace std::string_literals;

        auto strText = std::wstring_view(L"Hello World!!");
        auto strFileName = "%TEMP%\\test.parquet"s;
        auto strFileNameW = L"%TEMP%\\test.parquet"s;
        auto dwTextLen = strText.size() * sizeof(WCHAR);

        auto strPath = GetFilePath(strFileName);
        auto strPathW = GetFilePath(strFileNameW);

        {
            auto stream = std::make_shared<FileStream>(_L_);
            Assert::IsTrue(SUCCEEDED(stream->WriteTo(strPathW.c_str())));
            auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
            Assert::IsTrue(SUCCEEDED(arrow_stream->Open(stream)), L"Failed to open arrow stream");

            auto arrow_output_stream = std::make_shared<parquet::ArrowOutputStream>(arrow_stream);

            parquet::WriterProperties::Builder props_builder;
            props_builder.data_pagesize(4096 * 1024);
            props_builder.max_row_group_length(10000);
            props_builder.compression(parquet::Compression::ZSTD);
            // props_builder.compression("dip", parquet::Compression::SNAPPY);
            auto props = props_builder.build();

            std::vector<std::shared_ptr<arrow::Field>> schema_definition = {
                arrow::field("sip", arrow::int32(), false),
                arrow::field("dip", arrow::int32(), false),
                arrow::field("table", arrow::int32(), false),
                arrow::field("str", arrow::utf8(), false)};

            auto schema = std::make_shared<arrow::Schema>(schema_definition);

            std::unique_ptr<parquet::arrow::FileWriter> writer;
            parquet::arrow::FileWriter::Open(
                *schema,
                ::arrow::default_memory_pool(),
                arrow_output_stream,
                props,
                parquet::arrow::default_arrow_writer_properties(),
                &writer);

            for (int i = 0; i < 10; i++)
            {
                std::shared_ptr<arrow::Table> table = generate_table(schema, i, 10000);

                writer->WriteTable(*table, 10000);
            }
            writer->Close();
        }

        read_whole_file(strPathW.c_str());
        read_single_rowgroup(strPathW.c_str());
        read_single_column(strPathW.c_str(), 0);
        read_single_column(strPathW.c_str(), 1);
        read_single_column(strPathW.c_str(), 2);
        read_single_column(strPathW.c_str(), 3);
        read_single_column_chunk(strPath.c_str());

        // UtilDeleteTemporaryFile(_L_, strPathW.c_str());
    }

    std::wstring GetFilePath(const std::wstring& strFileName)
    {
        std::wstring retval;

        if (auto hr = GetOutputFile(strFileName.c_str(), retval); FAILED(hr))
            throw Orc::Exception(Fatal, hr, L"Failed to expand output file name (from string %s)", strFileName.c_str());
        return retval;
    }
    std::string GetFilePath(const std::string& strFileName)
    {

        if (auto [hr, strName] = AnsiToWide(_L_, strFileName); SUCCEEDED(hr))
        {
            std::wstring strPath;
            if (auto hr = GetOutputFile(strName.c_str(), strPath); SUCCEEDED(hr))
            {
                if (auto [hr, retval] = WideToAnsi(_L_, strPath); SUCCEEDED(hr))
                    return retval;
                else
                    throw Orc::Exception(
                        Fatal, hr, L"Failed to convert output file name to UTF8 (from string %s)", strPath.c_str());
            }
            else
                throw Orc::Exception(
                    Fatal, hr, L"Failed to expand output file name (from string %S)", strFileName.c_str());
        }
        else
            throw Orc::Exception(
                Fatal, hr, L"Failed to convert output file name to UTF16 (from string %S)", strFileName.c_str());
    }
};
}  // namespace Orc::Test::Parquet
