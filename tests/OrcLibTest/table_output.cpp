//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "OutputSpec.h"

#include "TableOutputWriter.h"
#include "TableOutput.h"

#include "Temporary.h"
#include "ParameterCheck.h"
#include "FileStream.h"
#include "MemoryStream.h"

#include <safeint.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(TableOutput)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(BasicTest)
    {
        using namespace Orc::TableOutput;
        using namespace std::string_view_literals;
        using namespace std::string_literals;
        using namespace msl::utilities;

        auto options = std::make_unique<CSV::Options>();

        auto stream_writer = Orc::TableOutput::GetCSVWriter(std::move(options));
        Assert::IsTrue((bool)stream_writer, L"Failed to instantiate parquet writer");

        constexpr auto mem = 0;

        std::shared_ptr<ByteStream> stream;

        if (mem)
        {
            auto mem_stream = std::make_shared<MemoryStream>();
            Assert::IsTrue((bool)mem_stream);

            auto hr = mem_stream->OpenForReadWrite();
            Assert::IsTrue(SUCCEEDED(hr), L"Failed to open memory stream");

            stream_writer->WriteToStream(mem_stream, false);
            stream = mem_stream;
        }
        else
        {
            auto file_stream = std::make_shared<FileStream>();
            Assert::IsTrue((bool)file_stream);

			std::wstring tempPath;
            tempPath.resize(1024);
			const auto length = GetTempPathW(SafeInt<DWORD>(tempPath.size()), tempPath.data());
            Assert::IsTrue(length);
			tempPath.resize(length);
            tempPath.append(L"\\test.csv");

            auto hr = file_stream->WriteTo(tempPath.c_str());
            Assert::IsTrue(SUCCEEDED(hr), L"Failed to open file stream");

            stream_writer->WriteToStream(file_stream);
            stream = file_stream;
        }

        Schema schema {{ColumnType::UInt32Type, L"FieldOne", L"One"},
                       {ColumnType::UTF16Type, L"FieldTwo", L"Two"},
                       {ColumnType::UInt64Type, L"FieldThree", L"Three"},
                       {ColumnType::UTF8Type, L"FieldFour", L"Four"},
                       {ColumnType::BoolType, L"FieldFive", L"Five"},
                       {ColumnType::UInt64Type, L"FieldSix", L"Six"},
                       {ColumnType::TimeStampType, L"FieldSeven", L"Seven", L"{YYYY}-{MM}-{DD} {hh}:{mm}:{ss}"},
                       {ColumnType::UInt32Type, L"FieldEigth", L"Eight"},
                       {ColumnType::FixedBinaryType, L"FieldNine", L"Nine", L"0x{:02X}"},
                       {ColumnType::EnumType, L"FieldTen", L"Ten"},
                       {ColumnType::EnumType, L"FieldEleven", L"Eleven"},
                       {ColumnType::FlagsType, L"FieldTwelve", L"Twelve"},
                       {ColumnType::FlagsType, L"FieldThirteen", L"Thirteen"}};

        auto& fixed = schema[L"FieldNine"sv];
        fixed.dwLen = 16;
        fixed.Format = L"{:02x}";

        auto& enum_ = schema[L"FieldTen"sv];
        enum_.EnumValues = {{L"EnumValOne"s, 0},
                            {L"EnumValTwo"s, 1},
                            {L"EnumValThree"s, 2},
                            {L"EnumValFour"s, 3},
                            {L"EnumValFive"s, 4},
                            {L"EnumValSix"s, 5}};

        auto& flags = schema[L"FieldTwelve"sv];
        flags.FlagsValues = {{L"BitOne"s, 1 << 0},
                             {L"BitTwo"s, 1 << 1},
                             {L"BitThree"s, 1 << 2},
                             {L"BitFour"s, 1 << 3},
                             {L"BitFive"s, 1 << 4},
                             {L"BitSix"s, 1 << 5}};

        stream_writer->SetSchema(schema);

        auto& output = *stream_writer;

        for (UINT i = 0; i < 1000; i++)
        {
            output.WriteInteger((DWORD)i);
            output.WriteFormated(L"This is a string ({})", i);
            output.WriteInteger((DWORDLONG)i * 2);
            output.WriteFormated("This is a string ({})", i);
            output.WriteBool(i % 2);
            output.WriteInteger((ULONGLONG)i);

            FILETIME now {0};
            GetSystemTimeAsFileTime(&now);
            output.WriteFileTime(now);

            output.WriteNothing();

            Buffer<BYTE, 16> bytes {
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

            output.WriteBytes((LPBYTE)bytes, 16);

            output.WriteEnum(i % 8);

            LPCWSTR nameflags[] = {L"ValOne", L"ValTwo", L"ValThree", L"ValFour", L"ValFive", NULL};

            output.WriteEnum(i % 8, nameflags);

            output.WriteFlags(i % 8);

            static const FlagsDefinition MachineDefs[] = {
                {IMAGE_FILE_MACHINE_UNKNOWN, L"MachineUnknown", L"MachineUnknown"},
                {IMAGE_FILE_MACHINE_I386, L"x86", L"X86 Machine"},
                {IMAGE_FILE_MACHINE_IA64, L"IA64", L"Titanium Machine"},
                {IMAGE_FILE_MACHINE_AMD64, L"x64", L"Machine"},
                {IMAGE_FILE_MACHINE_EBC, L"EFI", L"Machine"},
                {IMAGE_FILE_MACHINE_R3000, L"MIPS little-endian", L"MIPS little-endian Machine"},
                {IMAGE_FILE_MACHINE_R4000, L"MIPS little-endian", L"MIPS little-endian Machine"},
                {IMAGE_FILE_MACHINE_R10000, L"MIPS little-endian", L"MIPS little-endian Machine"},
                {IMAGE_FILE_MACHINE_WCEMIPSV2, L"MIPS little-endian WCE v2", L"MIPS little-endian WCE v2 Machine"},
                {IMAGE_FILE_MACHINE_ALPHA, L"ALPHA64", L"ALPHA64 Machine"},
                {IMAGE_FILE_MACHINE_AXP64, L"Alpha_AXP", L"Alpha_AXP Machine"},
                {IMAGE_FILE_MACHINE_SH3, L"SH3 little-endian", L"SH3 little-endian Machine"},
                {IMAGE_FILE_MACHINE_SH3DSP, L"SH3", L"SH3 Machine"},
                {IMAGE_FILE_MACHINE_SH3E, L"SH3E little-endian", L"SH3E little-endian Machine"},
                {IMAGE_FILE_MACHINE_SH4, L"SH4 little-endian", L"SH4 little-endian Machine"},
                {IMAGE_FILE_MACHINE_SH5, L"SH5", L"SH5 Machine"},
                {IMAGE_FILE_MACHINE_ARM, L"ARM Little-Endian", L"ARM Little-Endian Machine"},
                {IMAGE_FILE_MACHINE_THUMB, L"THUMB", L"THUMB Machine"},
                {IMAGE_FILE_MACHINE_AM33, L"AM33", L"AM33 Machine"},
                {IMAGE_FILE_MACHINE_POWERPC, L"IBM PowerPC Little-Endian", L"IBM PowerPC Little-Endian Machine"},
                {IMAGE_FILE_MACHINE_POWERPCFP, L"IBM PowerPC", L"IBM PowerPC Machine"},
                {IMAGE_FILE_MACHINE_MIPS16, L"MIPS", L"MIPS Machine"},
                {IMAGE_FILE_MACHINE_MIPSFPU, L"MIPS", L"MIPSMachine"},
                {IMAGE_FILE_MACHINE_MIPSFPU16, L"MIPS", L"MIPS Machine"},
                {IMAGE_FILE_MACHINE_TRICORE, L"Infineon", L"Infineon Machine"},
                {IMAGE_FILE_MACHINE_CEF, L"CEF", L"CEF Machine"},
                {IMAGE_FILE_MACHINE_M32R, L"M32R little-endian", L"M32R little-endian Machine"},
                {IMAGE_FILE_MACHINE_CEE, L"CEE", L"CEE Machine"},
                {(DWORD)-1, NULL, NULL}};

            output.WriteExactFlags(MachineDefs[i % 28].dwFlag, MachineDefs);

            output.WriteEndOfLine();
        }

        stream_writer->Close();

        stream->Close();
        if (auto mem_stream = std::dynamic_pointer_cast<MemoryStream>(stream); mem_stream)
        {
            auto buffer = mem_stream->GetConstBuffer();
            Log::Info(L"Table has {} bytes", buffer.GetCount());
        }
        else
        {
            Log::Info(L"Table has {} bytes", stream->GetSize());
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
}  // namespace Orc::Test
