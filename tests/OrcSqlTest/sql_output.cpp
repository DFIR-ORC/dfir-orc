//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"

#include "SqlOutputWriter.h"
#include "TableOutputWriter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace std::string_literals;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test::Sql {
TEST_CLASS(SqlOutput)
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
    TEST_METHOD(LoadSqlExtension)
    {
        auto extension = Orc::ExtensionLibrary::GetLibrary<SqlOutputWriter>(_L_);
        Assert::IsTrue((bool)extension, L"Failed to load SqlOutputWriter extension");

        auto connection = extension->ConnectionFactory(_L_, nullptr);
        Assert::IsTrue((bool)connection, L"Failed to create connection instance");
    }
    TEST_METHOD(Connection)
    {
        auto extension = Orc::ExtensionLibrary::GetLibrary<SqlOutputWriter>(_L_);
        Assert::IsTrue((bool)extension, L"Failed to load SqlOutputWriter extension");

        auto connection = extension->ConnectionFactory(_L_, nullptr);
        Assert::IsTrue((bool)connection, L"Failed to create connection instance");

        connection->Connect(
            L"Driver={SQL Server Native Client 11.0};Server=localhost;Network "
            L"Library=dbmslpcn;Trusted_Connection=yes;Database=OrcTest;MARS_Connection=yes");
        Assert::IsTrue(connection->IsConnected(), L"Failed to connect to Sql Server");

        if (connection->IsTablePresent(L"TestTable"s))
        {
            connection->DropTable(L"TestTable"s);
        }

        auto writer = extension->ConnectTableFactory(_L_, nullptr);
        Assert::IsTrue((bool)writer, L"Failed to create writer instance");

        writer->SetConnection(connection);
    }
    TEST_METHOD(CreateTable)
    {
        auto extension = Orc::ExtensionLibrary::GetLibrary<SqlOutputWriter>(_L_);
        Assert::IsTrue((bool)extension, L"Failed to load SqlOutputWriter extension");

        auto connection = extension->ConnectionFactory(_L_, nullptr);
        Assert::IsTrue((bool)connection, L"Failed to create connection instance");

        connection->Connect(
            L"Driver={SQL Server Native Client 11.0};Server=localhost;Network "
            L"Library=dbmslpcn;Trusted_Connection=yes;Database=OrcTest;MARS_Connection=yes");
        Assert::IsTrue(connection->IsConnected(), L"Failed to connect to Sql Server");

        if (connection->IsTablePresent(L"TestTable"s))
        {
            connection->DropTable(L"TestTable"s);
        }

        auto schema = TableOutput::Schema {{TableOutput::ColumnType::UTF16Type, L"Text"},
                                           {TableOutput::ColumnType::UInt32Type, L"DWORD"}};

        connection->CreateTable(L"TestTable"s, schema);

        auto writer = extension->ConnectTableFactory(_L_, nullptr);
        Assert::IsTrue((bool)writer, L"Failed to create writer instance");

        writer->SetConnection(connection);
        writer->SetSchema(schema);
        writer->BindColumns(L"TestTable");

        auto& output = writer->GetTableOutput();

        for (int i = 0; i < 100; i++)
        {
            wstring text = L"Hello "s + std::to_wstring(i);

            output.WriteString(text.c_str());
            output.WriteInteger((DWORD)i);
            output.WriteEndOfLine();
        }
        writer->Close();
    }
};
}  // namespace Orc::Test::Sql
