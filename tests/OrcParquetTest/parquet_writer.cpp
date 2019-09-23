//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "LogFileWriter.h"

#include "TableOutputWriter.h"

#include "FileStream.h"
#include "ParameterCheck.h"
#include "Temporary.h"
#include "FileStream.h"
#include "OrcException.h"
#include "ParquetWriter.h"
#include "ParquetStream.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace std::string_literals;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test::Parquet {
TEST_CLASS(ParquetWriter)
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

    TEST_METHOD(WriteSimpleTypes)
    {
        using namespace std::string_literals;
        using namespace std::string_view_literals;
        using namespace Orc::TableOutput;

        auto stream_writer = Orc::TableOutput::GetParquetWriter(_L_, nullptr);
        Assert::IsTrue((bool)stream_writer, L"Failed to instantiate parquet writer");

        Schema schema {{ColumnType::UInt32Type, L"FieldOne"sv},
                       {ColumnType::UTF16Type, L"FieldTwo"sv},
                       {ColumnType::UInt64Type, L"FieldThree"sv},
                       {ColumnType::UTF8Type, L"FieldFour"sv},
                       {ColumnType::BoolType, L"FieldFive"sv},
                       {ColumnType::UInt64Type, L"FieldSix"sv},
                       {ColumnType::TimeStampType, L"FieldSeven"sv},
                       {ColumnType::EnumType, L"FieldEight"sv}};

        auto& enum_ = schema[L"FieldEight"sv];
        enum_.EnumValues = {{L"EnumValOne"s, 0},
                            {L"EnumValTwo"s, 1},
                            {L"EnumValThree"s, 2},
                            {L"EnumValFour"s, 3},
                            {L"EnumValFive"s, 4},
                            {L"EnumValSix"s, 5}};

        Assert::IsTrue(SUCCEEDED(stream_writer->SetSchema(schema)), L"Failed to set parquet Schema");

        auto strPath = GetFilePath(L"%TEMP%\\test.parquet"s);

        auto hr = stream_writer->WriteToFile(strPath.c_str());
        Assert::IsTrue(SUCCEEDED(hr), L"Failed to write to parquet stream");

        auto& output = stream_writer->GetTableOutput();

        for (UINT i = 0; i < 1000; i++)
        {
            output.WriteInteger((DWORD)i);
            output.WriteFormated(L"This is a wide string ({})", i);
            output.WriteInteger((DWORDLONG)i * 2);
            output.WriteFormated("This is a string ({})", i);
            output.WriteBool(i % 2);
            output.WriteInteger((ULONGLONG)i);

            FILETIME now {0};
            GetSystemTimeAsFileTime(&now);
            output.WriteFileTime(now);

            output.WriteEnum(i % 6);

            output.WriteEndOfLine();
        }

        stream_writer->Close();
    }

    TEST_METHOD(ReadSimpeTypes)
    {
        auto file_stream = std::make_shared<FileStream>(_L_);

        auto strPath = GetFilePath(L"%TEMP%\\test.parquet"s);
        file_stream->ReadFrom(strPath.c_str());

        auto arrow_stream = std::make_shared<Orc::TableOutput::Parquet::Stream>(_L_);
        arrow_stream->Open(file_stream);

        std::unique_ptr<parquet::arrow::FileReader> reader;
        PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(arrow_stream, arrow::default_memory_pool(), &reader));

        std::shared_ptr<arrow::Table> table;

        for (int i = 0; i < 1; i++)
        {
            PARQUET_THROW_NOT_OK(reader->ReadTable(&table));
            log::Info(_L_, L"Loaded %I64d rows in %d columns\r\n", table->num_rows(), table->num_columns());

            for (const auto& field : table->schema()->fields())
            {
                log::Info(_L_, L"Column %S %S\r\n", field->name().c_str(), field->type()->ToString().c_str());
            }

            arrow::TableBatchReader reader(*table);

            reader.set_chunksize(10);

            std::shared_ptr<arrow::RecordBatch> batch;
            while (reader.ReadNext(&batch).ok() && batch)
            {

                for (INT i = 0; i < batch->num_columns(); i++)
                {
                    auto data = batch->column(i);

                    std::ostringstream output;
                    arrow::PrettyPrint(*data, 0, &output);

                    log::Info(_L_, L"Data: %S\r\n", output.str().c_str());
                }
            }
        }
    }

    std::wstring GetFilePath(const std::wstring& strFileName)
    {
        std::wstring retval;

        if (auto hr = GetOutputFile(strFileName.c_str(), retval); FAILED(hr))
            throw Orc::Exception(Fatal, hr, L"Failed to expand output file name (from string %s)", strFileName.c_str());
        return retval;
    }
};
}  // namespace Orc::Test::Parquet