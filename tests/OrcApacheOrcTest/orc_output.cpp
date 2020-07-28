//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "TableOutputWriter.h"

#include "FileStream.h"
#include "ParameterCheck.h"
#include "Temporary.h"
#include "FileStream.h"
#include "OrcException.h"
#include "WideAnsi.h"

#include "ApacheOrcMemoryPool.h"
#include "ApacheOrcStream.h"

#include "orc/OrcFile.hh"

#include "UnitTestHelper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace std::string_literals;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test::ApacheOrc {
TEST_CLASS(OrcWriter)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}
    TEST_METHOD_CLEANUP(Finalize) {}
    TEST_METHOD(WriteLocalFile)
    {
        using namespace orc;
        auto strPath = GetFilePath("%TEMP%\\testLocalFile.orc"s);
        std::unique_ptr<OutputStream> outStream = writeLocalFile(strPath);

        WriteSimpleData(outStream);
    }

    TEST_METHOD(WriteToStream)
    {
        using namespace orc;
        auto strPath = GetFilePath(L"%TEMP%\\testStream.orc"s);

        auto fileStream = std::make_shared<Orc::FileStream>();

        Assert::IsTrue(SUCCEEDED(fileStream->WriteTo(strPath.c_str())));

        auto outStream = std::make_unique<Orc::TableOutput::ApacheOrc::Stream>();

        Assert::IsTrue(SUCCEEDED(outStream->Open(fileStream)));

        WriteSimpleData(std::unique_ptr<orc::OutputStream>(std::move(outStream)));
    }

    TEST_METHOD(WriteToStreamWithNulls)
    {
        using namespace orc;
        auto strPath = GetFilePath(L"%TEMP%\\testStreamWithNulls.orc"s);

        auto fileStream = std::make_shared<Orc::FileStream>();

        Assert::IsTrue(SUCCEEDED(fileStream->WriteTo(strPath.c_str())));

        auto outStream = std::make_unique<Orc::TableOutput::ApacheOrc::Stream>();

        Assert::IsTrue(SUCCEEDED(outStream->Open(fileStream)));

        WriteDataWithNulls(std::unique_ptr<orc::OutputStream>(std::move(outStream)));
    }

    TEST_METHOD(SimpleWriterTest)
    {
        using namespace std::string_literals;
        using namespace std::string_view_literals;
        using namespace Orc::TableOutput;

        auto options = std::make_unique<TableOutput::ApacheOrc::Options>();

        options->BatchSize = 102400;

        auto stream_writer = Orc::TableOutput::GetApacheOrcWriter(std::move(options));
        Assert::IsTrue((bool)stream_writer, L"Failed to instantiate orc writer");

        Schema schema {
            {ColumnType::UInt32Type, L"FieldOne"sv},
            {ColumnType::UTF16Type, L"FieldTwo"sv},
            {ColumnType::UInt64Type, L"FieldThree"sv},
            {ColumnType::UTF8Type, L"FieldFour"sv},
            {ColumnType::BoolType, L"FieldFive"sv},
            {ColumnType::UInt64Type, L"FieldSix"sv}};

        Assert::IsTrue(SUCCEEDED(stream_writer->SetSchema(schema)), L"Failed to set parquet Schema");

        auto strPath = GetFilePath(L"%TEMP%\\test.orc"s);

        auto hr = stream_writer->WriteToFile(strPath.c_str());
        Assert::IsTrue(SUCCEEDED(hr), L"Failed to write to orc stream");

        auto& output = *stream_writer;

        for (UINT i = 0; i < 1000000; i++)
        {
            output.WriteInteger((DWORD)i);
            output.WriteString(L"This is a wide string");
            output.WriteInteger((DWORDLONG)i * 2);
            output.WriteString("This is a string");
            output.WriteBool(i % 2);
            output.WriteInteger((ULONGLONG)i);

            output.WriteEndOfLine();
        }

        stream_writer->Close();
    }

    void WriteSimpleData(const std::unique_ptr<orc::OutputStream>& output)
    {
        using namespace orc;

        std::unique_ptr<Type> schema = createStructType();

        schema->addStructField("x", createPrimitiveType(TypeKind::INT));
        schema->addStructField("y", createPrimitiveType(TypeKind::INT));
        schema->addStructField("s", createPrimitiveType(TypeKind::STRING));

        auto strSchema = schema->toString();

        WriterOptions options;
        std::unique_ptr<Writer> writer = createWriter(*schema, output.get(), options);

        uint64_t batchSize = 10240, rowCount = 1000;
        std::unique_ptr<ColumnVectorBatch> batch = writer->createRowBatch(batchSize);
        StructVectorBatch* root = dynamic_cast<StructVectorBatch*>(batch.get());
        LongVectorBatch* x = dynamic_cast<LongVectorBatch*>(root->fields[0]);
        LongVectorBatch* y = dynamic_cast<LongVectorBatch*>(root->fields[1]);
        StringVectorBatch* s = dynamic_cast<StringVectorBatch*>(root->fields[2]);

        std::unique_ptr<orc::MemoryPool> pool =
            std::make_unique<Orc::TableOutput::ApacheOrc::MemoryPool>(10 * 1024 * 1024);

        uint64_t rows = 0;
        for (uint64_t i = 0; i < rowCount; ++i)
        {
            x->data[rows] = i;
            y->data[rows] = i * 3;

            std::string str = "Hello world!";
            str.append(std::to_string(i));

            auto data = (char*)pool->malloc(str.size());
            memcpy(data, str.c_str(), str.size());

            s->data[rows] = data;
            s->length[rows] = str.size();

            rows++;

            if (rows == batchSize)
            {
                root->numElements = rows;
                x->numElements = rows;
                y->numElements = rows;
                s->numElements = rows;

                writer->add(*batch);
                pool = std::make_unique<Orc::TableOutput::ApacheOrc::MemoryPool>(10 * 1024 * 1024);
                rows = 0;
            }
        }

        if (rows != 0)
        {
            root->numElements = rows;
            x->numElements = rows;
            y->numElements = rows;
            s->numElements = rows;

            writer->add(*batch);
            rows = 0;
        }

        writer->close();
    }

    void WriteDataWithNulls(const std::unique_ptr<orc::OutputStream>& output)
    {
        using namespace orc;

        std::unique_ptr<Type> schema = createStructType();

        schema->addStructField("x", createPrimitiveType(TypeKind::INT));
        schema->addStructField("y", createPrimitiveType(TypeKind::INT));
        schema->addStructField("s", createPrimitiveType(TypeKind::STRING));

        auto strSchema = schema->toString();

        WriterOptions options;
        std::unique_ptr<Writer> writer = createWriter(*schema, output.get(), options);

        uint64_t batchSize = 10240, rowCount = 1000;
        std::unique_ptr<ColumnVectorBatch> batch = writer->createRowBatch(batchSize);
        StructVectorBatch* root = dynamic_cast<StructVectorBatch*>(batch.get());
        LongVectorBatch* x = dynamic_cast<LongVectorBatch*>(root->fields[0]);
        LongVectorBatch* y = dynamic_cast<LongVectorBatch*>(root->fields[1]);
        StringVectorBatch* s = dynamic_cast<StringVectorBatch*>(root->fields[2]);

        std::unique_ptr<orc::MemoryPool> pool =
            std::make_unique<Orc::TableOutput::ApacheOrc::MemoryPool>(10 * 1024 * 1024);

        uint64_t rows = 0;
        for (uint64_t i = 0; i < rowCount; ++i)
        {

            // if (i % 3 == 0)
            {
                x->data[rows] = i;
                y->data[rows] = i * 3;

                std::string str = "Hello world!";
                str.append(std::to_string(i));

                auto data = (char*)pool->malloc(str.size());
                memcpy(data, str.c_str(), str.size());

                s->data[rows] = data;
                s->length[rows] = str.size();

                if (i % 3)
                    x->notNull[rows] = true;
                else
                {
                    x->notNull[rows] = false;
                    x->hasNulls = true;
                }

                y->notNull[rows] = true;
                if (i % 2)
                    s->notNull[rows] = true;
                else
                {
                    s->hasNulls = true;
                    s->notNull[rows] = false;
                }
            }
            // else
            //{
            //	x->notNull[rows] = false;
            //	y->notNull[rows] = false;
            //	s->notNull[rows] = false;
            //}

            rows++;

            if (rows == batchSize)
            {
                root->numElements = rows;
                x->numElements = rows;
                y->numElements = rows;
                s->numElements = rows;

                writer->add(*batch);
                pool = std::make_unique<Orc::TableOutput::ApacheOrc::MemoryPool>(10 * 1024 * 1024);
                rows = 0;
            }
        }

        if (rows != 0)
        {
            root->numElements = rows;
            x->numElements = rows;
            y->numElements = rows;
            s->numElements = rows;

            writer->add(*batch);
            rows = 0;
        }

        writer->close();
    }

    std::wstring GetFilePath(const std::wstring& strFileName)
    {
        std::wstring retval;

        if (auto hr = GetOutputFile(strFileName.c_str(), retval); FAILED(hr))
            throw Orc::Exception(
                Severity::Fatal, hr, L"Failed to expand output file name (from string {})", strFileName);
        return retval;
    }
    std::string GetFilePath(const std::string& strFileName)
    {

        if (auto [hr, strName] = AnsiToWide(strFileName); SUCCEEDED(hr))
        {
            std::wstring strPath;
            if (auto hr = GetOutputFile(strName.c_str(), strPath); SUCCEEDED(hr))
            {
                if (auto [hr, retval] = WideToAnsi(strPath); SUCCEEDED(hr))
                    return retval;
                else
                    throw Orc::Exception(
                        Severity::Fatal, hr, L"Failed to convert output file name to UTF8 (from string {})", strPath);
            }
            else
                throw Orc::Exception(
                    Severity::Fatal, hr, L"Failed to expand output file name (from string {})", strName);
        }
        else
            throw Orc::Exception(
                Severity::Fatal, hr, L"Failed to convert output file name to UTF16 (from string {})", strName);
    }
};
}  // namespace Orc::Test::ApacheOrc
