//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"

#include "StructuredOutputWriter.h"
#include "RobustStructuredWriter.h"
#include "XmlOutputWriter.h"
#include "JSONOutputWriter.h"

#include "OutputSpec.h"

#include "Temporary.h"

#include "FileStream.h"
#include "CryptoHashStream.h"
#include "MemoryStream.h"

#include "XmlLiteExtension.h"

#include "NtfsDataStructures.h"

#include "Unicode.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(StructuredOutputTest)
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

    HRESULT WriterSingleTest(const std::shared_ptr<StructuredOutput::IWriter>& _writer)
    {
        auto writer = std::dynamic_pointer_cast<StructuredOutput::LegacyWriter>(_writer);

        Assert::IsNotNull(writer.get());

        Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test")));

        // Basic elements
        Assert::IsTrue(SUCCEEDED(writer->WriteNamed(L"test_element", L"test_value")));

        Assert::IsTrue(SUCCEEDED(writer->WriteNamed(L"test_path", L"c:\\windows\\system32\\kernel32.dll")));

        Assert::IsTrue(SUCCEEDED(
            writer->WriteNamedFormated(L"test_format", L"%s %S %X %d", L"test", "alternate", 24, 56)));

        {
            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test_boolean")));
            Assert::IsTrue(SUCCEEDED(writer->WriteNamed(L"test_true", true)));
            Assert::IsTrue(SUCCEEDED(writer->WriteNamed(L"test_false", false)));
            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test_boolean")));
        }

        {
            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test_integer")));

            // Long Integer values
            const ULONGLONG ull = (ULONGLONG)46412874515674154LL;
            Assert::IsTrue(SUCCEEDED(writer->WriteNameValuePair(L"test_ulonglong", ull)));
            Assert::IsTrue(SUCCEEDED(writer->WriteNameValuePair(L"test_ulonglong_hex", ull, true)));

            LARGE_INTEGER li;
            li.QuadPart = ull;
            Assert::IsTrue(SUCCEEDED(writer->WriteNameValuePair(L"test_large_integer", li)));
            Assert::IsTrue(SUCCEEDED(writer->WriteNameValuePair(L"test_large_integer_hex", li, true)));
            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test_integer")));
        }

        {
            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test_file_size")));
            Assert::IsTrue(SUCCEEDED(writer->WriteNameSizePair(L"test_size", 546874312)));
            Assert::IsTrue(SUCCEEDED(writer->WriteNameSizePair(L"test_size_hex", 546874312, true)));
            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test_file_size")));
        }

        {
            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test_file_attributes")));
            Assert::IsTrue(SUCCEEDED(writer->WriteNameAttributesPair(
                L"test_attrib", FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY)));
            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test_file_attributes")));
        }
        {
            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test_enum_flags")));

            LPCWSTR szValues[] = {L"Value0", L"Value1", L"Value2", NULL};

            Assert::IsTrue(SUCCEEDED(writer->WriteNameEnumPair(L"test_enum", 1, szValues)));

            static const Orc::FlagsDefinition AttrTypeDefs[] = {
                {$UNUSED, L"$UNUSED", L"$UNUSED"},
                {$STANDARD_INFORMATION, L"$STANDARD_INFORMATION", L"$STANDARD_INFORMATION"},
                {$ATTRIBUTE_LIST, L"$ATTRIBUTE_LIST", L"$ATTRIBUTE_LIST"},
                {$FILE_NAME, L"$FILE_NAME", L"$FILE_NAME"},
                {$OBJECT_ID, L"$OBJECT_ID", L"$OBJECT_ID"},
                {$SECURITY_DESCRIPTOR, L"$SECURITY_DESCRIPTOR", L"$SECURITY_DESCRIPTOR"},
                {$VOLUME_NAME, L"$VOLUME_NAME", L"$VOLUME_NAME"},
                {$VOLUME_INFORMATION, L"$VOLUME_INFORMATION", L"$VOLUME_INFORMATION"},
                {$DATA, L"$DATA", L"$DATA"},
                {$INDEX_ROOT, L"$INDEX_ROOT", L"$INDEX_ROOT"},
                {$INDEX_ALLOCATION, L"$INDEX_ALLOCATION", L"$INDEX_ALLOCATION"},
                {$BITMAP, L"$BITMAP", L"$BITMAP"},
                {$REPARSE_POINT, L"$REPARSE_POINT", L"$REPARSE_POINT"},
                {$EA_INFORMATION, L"$EA_INFORMATION", L"$EA_INFORMATION"},
                {$EA, L"$EA", L"$EA"},
                {$LOGGED_UTILITY_STREAM, L"$LOGGED_UTILITY_STREAM", L"$LOGGED_UTILITY_STREAM"},
                {$FIRST_USER_DEFINED_ATTRIBUTE, L"$FIRST_USER_DEFINED_ATTRIBUTE", L"$FIRST_USER_DEFINED_ATTRIBUTE"},
                {$END, L"$END", L"$END"}};

            Assert::IsTrue(SUCCEEDED(writer->WriteNameExactFlagsPair(L"test_flag_exact", $INDEX_ROOT, AttrTypeDefs)));

            static const Orc::FlagsDefinition g_Reasons[] = {
                {USN_REASON_BASIC_INFO_CHANGE,
                 L"BASIC_INFO_CHANGE",
                 L"A user has either changed one or more file or directory attributes (for example, the read-only, "
                 L"hidden, system, archive, or sparse attribute), or one or more time stamps."},
                {USN_REASON_CLOSE, L"CLOSE", L"The file or directory is closed."},
                {USN_REASON_COMPRESSION_CHANGE,
                 L"COMPRESSION_CHANGE",
                 L"The compression state of the file or directory is changed from or to compressed."},
                {USN_REASON_DATA_EXTEND, L"DATA_EXTEND", L"The file or directory is extended (added to)."},
                {USN_REASON_DATA_OVERWRITE, L"DATA_OVERWRITE", L"The data in the file or directory is overwritten."},
                {USN_REASON_DATA_TRUNCATION, L"DATA_TRUNCATION", L"The file or directory is truncated."},
                {USN_REASON_EA_CHANGE,
                 L"EA_CHANGE",
                 L"The user made a change to the extended attributes of a file or directory. These NTFS file system "
                 L"attributes are not accessible to Windows-based applications."},
                {USN_REASON_ENCRYPTION_CHANGE,
                 L"ENCRYPTION_CHANGE",
                 L"The file or directory is encrypted or decrypted."},
                {USN_REASON_FILE_CREATE, L"FILE_CREATE", L"The file or directory is created for the first time."},
                {USN_REASON_FILE_DELETE, L"FILE_DELETE", L"The file or directory is deleted."},
                {USN_REASON_HARD_LINK_CHANGE,
                 L"HARD_LINK_CHANGE",
                 L"An NTFS file system hard link is added to or removed from the file or directory. An NTFS file "
                 L"system hard link, similar to a POSIX hard link, is one of several directory entries that see the "
                 L"same file or directory."},
                {USN_REASON_INDEXABLE_CHANGE,
                 L"INDEXABLE_CHANGE",
                 L"A user changes the FILE_ATTRIBUTE_NOT_CONTENT_INDEXED attribute. That is, the user changes the file "
                 L"or directory from one where content can be indexed to one where content cannot be indexed, or vice "
                 L"versa. Content indexing permits rapid searching of data by building a database of selected "
                 L"content."},
                {USN_REASON_NAMED_DATA_EXTEND,
                 L"NAMED_DATA_EXTEND",
                 L"The one or more named data streams for a file are extended (added to)."},
                {USN_REASON_NAMED_DATA_OVERWRITE,
                 L"NAMED_DATA_OVERWRITE",
                 L"The data in one or more named data streams for a file is overwritten."},
                {USN_REASON_NAMED_DATA_TRUNCATION,
                 L"NAMED_DATA_TRUNCATION",
                 L"The one or more named data streams for a file is truncated."},
                {USN_REASON_OBJECT_ID_CHANGE,
                 L"OBJECT_ID_CHANGE",
                 L"The object identifier of a file or directory is changed."},
                {USN_REASON_RENAME_NEW_NAME,
                 L"RENAME_NEW_NAME",
                 L"A file or directory is renamed, and the file name in the USN_RECORD structure is the new name."},
                {USN_REASON_RENAME_OLD_NAME,
                 L"RENAME_OLD_NAME",
                 L"The file or directory is renamed, and the file name in the USN_RECORD structure is the previous "
                 L"name."},
                {USN_REASON_REPARSE_POINT_CHANGE,
                 L"REPARSE_POINT_CHANGE",
                 L"The reparse point that is contained in a file or directory is changed, or a reparse point is added "
                 L"to or deleted from a file or directory."},
                {USN_REASON_SECURITY_CHANGE,
                 L"SECURITY_CHANGE",
                 L"A change is made in the access rights to a file or directory."},
                {USN_REASON_STREAM_CHANGE,
                 L"STREAM_CHANGE",
                 L"A named stream is added to or removed from a file, or a named stream is renamed."},
                {USN_REASON_TRANSACTED_CHANGE, L"TRANSACTED_CHANGE", L"A change occured within a transaction"},
                {0xFFFFFFFF, NULL, NULL}};
            Assert::IsTrue(SUCCEEDED(writer->WriteNameFlagsPair(
                L"test_flags", USN_REASON_FILE_CREATE | USN_REASON_DATA_EXTEND | USN_REASON_CLOSE, g_Reasons, L'|')));

            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test_enum_flags")));

            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"test_binary_buffer")));

            Buffer<BYTE, 16> MD5 = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                    13, 14, 15};

            writer->WriteNamed(L"MD5", MD5.get(), MD5.size(), false);

            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"sha1")));
            {
                Buffer<BYTE, 20> SHA1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
                writer->Write(SHA1.get(), SHA1.size(), false);
            }
            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"sha1")));

            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test_binary_buffer")));

            Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"simple_data")));
            {
                Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"bool")));
                Assert::IsTrue(SUCCEEDED(writer->Write(true)));
                Assert::IsTrue(SUCCEEDED(writer->EndElement(L"bool")));

                Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"dword")));
                Assert::IsTrue(SUCCEEDED(writer->Write((UINT32) 0x01020304LU)));
                Assert::IsTrue(SUCCEEDED(writer->EndElement(L"dword")));

                Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"dword_hex")));
                Assert::IsTrue(SUCCEEDED(writer->Write((UINT32) 0x01020304LU, true)));
                Assert::IsTrue(SUCCEEDED(writer->EndElement(L"dword_hex")));

                Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"longlong")));
                Assert::IsTrue(SUCCEEDED(writer->Write(0x0102030405060708LLU)));
                Assert::IsTrue(SUCCEEDED(writer->EndElement(L"longlong")));

                Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"longlong")));
                Assert::IsTrue(SUCCEEDED(writer->Write(0x0102030405060708LLU, true)));
                Assert::IsTrue(SUCCEEDED(writer->EndElement(L"longlong")));

                Assert::IsTrue(SUCCEEDED(writer->BeginElement(L"string")));
                Assert::IsTrue(SUCCEEDED(writer->Write(L"Hello world!!")));
                Assert::IsTrue(SUCCEEDED(writer->EndElement(L"string")));

            }
            Assert::IsTrue(SUCCEEDED(writer->EndElement(L"simple_data")));
        }

        Assert::IsTrue(SUCCEEDED(writer->EndElement(L"test")));

        Assert::IsTrue(SUCCEEDED(writer->Close()));

        return S_OK;
    }

    HRESULT CompareTestResult(const std::shared_ptr<ByteStream>& result_stream, LPCWSTR szExpectedSHA1)
    {
        // Compare with expected result;

        auto hash_stream = std::make_shared<CryptoHashStream>(_L_);

        Assert::IsTrue(SUCCEEDED(hash_stream->OpenToWrite(CryptoHashStream::Algorithm::SHA1, nullptr)));

        ULONGLONG ullBytesWritten = 0LL;
        Assert::IsTrue(SUCCEEDED(result_stream->CopyTo(hash_stream, 4096, &ullBytesWritten)));

        {
            std::wstring sha1;
            Assert::IsTrue(SUCCEEDED(hash_stream->GetHash(CryptoHashStream::Algorithm::SHA1, sha1)));
            Assert::AreEqual(szExpectedSHA1, sha1.c_str(), L"Structured Output differ from expected result");
        }
        return S_OK;
    }

    TEST_METHOD(StructuredOutputBasicTest)
    {
        HRESULT hr = E_FAIL;

        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        auto result_stream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;

        auto writer = StructuredOutputWriter::GetWriter(_L_, result_stream, OutputSpec::Kind::XML, std::move(options));

        Assert::IsTrue(SUCCEEDED(WriterSingleTest(writer)));
        Assert::IsTrue(SUCCEEDED(CompareTestResult(result_stream, L"97AA814567B5B37150F76FE377CAC1D174DB5321")));

        writer.reset();

        result_stream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

        options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF16;
        writer = StructuredOutputWriter::GetWriter(_L_, result_stream, OutputSpec::Kind::XML, std::move(options));

        Assert::IsTrue(SUCCEEDED(WriterSingleTest(writer)));
        Assert::IsTrue(SUCCEEDED(CompareTestResult(result_stream, L"FE70F58C976F536ABD8294EC3B6C61438E92C465")));
    }

    TEST_METHOD(RobustStructuredOutputTest)
    {
        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        HRESULT hr = E_FAIL;

        auto result_stream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;

        auto writer = StructuredOutputWriter::GetWriter(_L_, result_stream, OutputSpec::Kind::XML, std::move(options));

        auto robust_writer =
            std::make_shared<RobustStructuredWriter>(_L_, std::dynamic_pointer_cast<StructuredOutputWriter>(writer));

        Assert::IsTrue(SUCCEEDED(WriterSingleTest(robust_writer)));
        Assert::IsTrue(SUCCEEDED(CompareTestResult(result_stream, L"97AA814567B5B37150F76FE377CAC1D174DB5321")));

        robust_writer.reset();

        result_stream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

        options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF16;

        writer = StructuredOutputWriter::GetWriter(_L_, result_stream, OutputSpec::Kind::XML, std::move(options));
        robust_writer =
            std::make_shared<RobustStructuredWriter>(_L_, std::dynamic_pointer_cast<StructuredOutputWriter>(writer));

        Assert::IsTrue(SUCCEEDED(WriterSingleTest(robust_writer)));
        Assert::IsTrue(SUCCEEDED(CompareTestResult(result_stream, L"FE70F58C976F536ABD8294EC3B6C61438E92C465")));
    }

    HRESULT WriteGargabeElementTest(const std::shared_ptr<ByteStream>& stream, WCHAR wGarbageCode)
    {
        HRESULT hr = E_FAIL;

        auto Silence = std::make_shared<LogFileWriter>();
        LogFileWriter::Initialize(Silence);
        Silence->SetConsoleLog(false);
        Silence->SetDebugLog(false);
        Silence->SetLogCallback(nullptr);
        Silence->SetVerboseLog(false);

        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;
        const auto _writer = StructuredOutputWriter::GetWriter(Silence, stream, OutputSpec::Kind::XML, std::move(options));

        const auto writer = std::dynamic_pointer_cast<StructuredOutput::LegacyWriter>(_writer);

        Assert::IsNotNull(writer.get());

        WCHAR szTestValue[MAX_PATH];
        wcscpy_s(szTestValue, L"TXT");

        szTestValue[1] = wGarbageCode;

        if (FAILED(hr = writer->BeginElement(szTestValue)))
        {
            return hr;
        }
        if (FAILED(hr = writer->WriteNameValuePair(L"value", L"test_value")))
        {
            return hr;
        }
        if (FAILED(hr = writer->EndElement(szTestValue)))
        {
            return hr;
        }

        Assert::IsTrue(SUCCEEDED(writer->Close()));

        return S_OK;
    }

    HRESULT WriteGargabePairTest(const std::shared_ptr<ByteStream>& stream, WCHAR wGarbageCode)
    {
        HRESULT hr = E_FAIL;

        auto Silence = std::make_shared<LogFileWriter>();
        LogFileWriter::Initialize(Silence);
        Silence->SetConsoleLog(false);
        Silence->SetDebugLog(false);
        Silence->SetLogCallback(nullptr);
        Silence->SetVerboseLog(false);

        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;

        const auto _writer = StructuredOutputWriter::GetWriter(Silence, stream, OutputSpec::Kind::XML, nullptr);
        const auto writer = std::dynamic_pointer_cast<StructuredOutput::LegacyWriter>(_writer);

        Assert::IsNotNull(writer.get());

        WCHAR szTestValue[MAX_PATH];
        wcscpy_s(szTestValue, L"TXT");

        szTestValue[1] = wGarbageCode;

        if (FAILED(hr = writer->BeginElement(L"test_element")))
        {
            return hr;
        }
        if (FAILED(hr = writer->WriteNameValuePair(L"value", szTestValue)))
        {
            return hr;
        }
        if (FAILED(hr = writer->EndElement(L"test_element")))
        {
            return hr;
        }

        Assert::IsTrue(SUCCEEDED(writer->Close()));

        return S_OK;
    }

    HRESULT WriteGargabeStringTest(const std::shared_ptr<ByteStream>& stream, WCHAR wGarbageCode)
    {
        HRESULT hr = E_FAIL;

        auto Silence = std::make_shared<LogFileWriter>();
        LogFileWriter::Initialize(Silence);
        Silence->SetConsoleLog(false);
        Silence->SetDebugLog(false);
        Silence->SetLogCallback(nullptr);
        Silence->SetVerboseLog(false);

        
        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;

        const auto _writer = StructuredOutputWriter::GetWriter(Silence, stream, OutputSpec::Kind::XML, nullptr);
        const auto writer = std::dynamic_pointer_cast<StructuredOutput::LegacyWriter>(_writer);

        Assert::IsNotNull(writer.get());

        WCHAR szTestValue[MAX_PATH];
        wcscpy_s(szTestValue, L"TXT");

        szTestValue[1] = wGarbageCode;

        if (FAILED(hr = writer->BeginElement(L"test_element")))
        {
            return hr;
        }
        if (FAILED(hr = writer->WriteString(szTestValue)))
        {
            return hr;
        }
        if (FAILED(hr = writer->EndElement(L"test_element")))
        {
            return hr;
        }

        Assert::IsTrue(SUCCEEDED(writer->Close()));

        return S_OK;
    }

    HRESULT WriteGargabeCommentTest(const std::shared_ptr<ByteStream>& stream, WCHAR wGarbageCode)
    {
        HRESULT hr = E_FAIL;

        auto Silence = std::make_shared<LogFileWriter>();
        LogFileWriter::Initialize(Silence);
        Silence->SetConsoleLog(false);
        Silence->SetDebugLog(false);
        Silence->SetLogCallback(nullptr);
        Silence->SetVerboseLog(false);

        
        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;
        
        const auto _writer = StructuredOutputWriter::GetWriter(Silence, stream, OutputSpec::Kind::XML, std::move(options));
        const auto writer = std::dynamic_pointer_cast<StructuredOutput::LegacyWriter>(_writer);

        Assert::IsNotNull(writer.get());

        WCHAR szTestValue[MAX_PATH];
        wcscpy_s(szTestValue, L"TXT");

        szTestValue[1] = wGarbageCode;

        if (FAILED(hr = writer->BeginElement(L"test_element")))
        {
            return hr;
        }
        if (FAILED(hr = writer->WriteComment(szTestValue)))
        {
            return hr;
        }
        if (FAILED(hr = writer->EndElement(L"test_element")))
        {
            return hr;
        }

        Assert::IsTrue(SUCCEEDED(writer->Close()));

        return S_OK;
    }

    HRESULT WriteSanitizedTest(const std::shared_ptr<ByteStream>& stream)
    {
        HRESULT hr = E_FAIL;

        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        auto Silence = std::make_shared<LogFileWriter>();
        LogFileWriter::Initialize(Silence);
        Silence->SetConsoleLog(false);
        Silence->SetDebugLog(false);
        Silence->SetLogCallback(nullptr);
        Silence->SetVerboseLog(false);

        auto options = std::make_unique<Orc::StructuredOutput::XML::Options>();
        options->Encoding = OutputSpec::Encoding::UTF8;
        const auto _writer =
            StructuredOutputWriter::GetWriter(Silence, stream, OutputSpec::Kind::XML, std::move(options));
        const auto writer = std::dynamic_pointer_cast<StructuredOutput::LegacyWriter>(_writer);

        Assert::IsNotNull(writer.get());

        WCHAR szTestValue[MAX_PATH];
        wcscpy_s(szTestValue, L"TXT");

        if (FAILED(hr = writer->BeginElement(L"sanitized")))
        {
            return hr;
        }
        for (WCHAR x = 0x01; x < 0xFFFF; x++)
        {

            szTestValue[1] = x;

            wstring strNoInvalidChars;
            wstring strSanitized;

            Assert::IsTrue(SUCCEEDED(
                ReplaceInvalidChars(xml_element_table, szTestValue, (DWORD)wcslen(szTestValue), strNoInvalidChars)));
            Assert::IsTrue(
                SUCCEEDED(SanitizeString(xml_attr_value_table, szTestValue, (DWORD)wcslen(szTestValue), strSanitized)));

            if (FAILED(hr = writer->BeginElement(strNoInvalidChars.c_str())))
            {
                return hr;
            }
            if (FAILED(hr = writer->WriteNameValuePair(L"value", strSanitized.c_str())))
            {
                return hr;
            }
            if (FAILED(hr = writer->EndElement(strNoInvalidChars.c_str())))
            {
                return hr;
            }
        }
        if (FAILED(hr = writer->EndElement(L"sanitized")))
        {
            return hr;
        }

        Assert::IsTrue(SUCCEEDED(writer->Close()));

        return S_OK;
    }

    TEST_METHOD(StructuredOutputGarbageElementTest)
    {
        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        HRESULT hr = E_FAIL;

        for (WCHAR x = 0x01; x < 0xFFFF; x++)
        {
            auto result_stream = std::make_shared<MemoryStream>(_L_);
            Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

            hr = WriteGargabeElementTest(result_stream, x);

            if (IsUnicodeValid(xml_element_table, x))
            {
                Assert::IsTrue(SUCCEEDED(hr), L"XMLWriter test was expected to accept code");
            }
            else
            {
                Assert::IsTrue(FAILED(hr), L"XMLWriter test was expected to reject code");
            }
        }
    }

    TEST_METHOD(StructuredOutputGarbagePairTest)
    {
        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        HRESULT hr = E_FAIL;

        for (WCHAR x = 0x01; x < 0xFFFF; x++)
        {
            auto result_stream = std::make_shared<MemoryStream>(_L_);
            Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

            hr = WriteGargabePairTest(result_stream, x);

            if (IsUnicodeValid(xml_attr_value_table, x))
            {
                Assert::IsTrue(SUCCEEDED(hr), L"XMLWriter test was expected to accept code");
            }
            else
            {
                Assert::IsTrue(FAILED(hr), L"XMLWriter test was expected to reject code");
            }
        }
    }

    TEST_METHOD(StructuredOutputGarbageStringTest)
    {
        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        HRESULT hr = E_FAIL;

        for (WCHAR x = 0x01; x < 0xFFFF; x++)
        {
            auto result_stream = std::make_shared<MemoryStream>(_L_);
            Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));
            hr = WriteGargabeStringTest(result_stream, x);

            // if (SUCCEEDED(hr))
            //{
            //	log::Info(L, L"{ /* %.4X */ true},", x);
            //}
            // else
            //{
            //	log::Info(L, L"{ /* %.4X */ false},", x);
            //}

            if (IsUnicodeValid(xml_string_table, x))
            {
                Assert::IsTrue(SUCCEEDED(hr), L"XMLWriter test was expected to accept code");
            }
            else
            {
                Assert::IsTrue(FAILED(hr), L"XMLWriter test was expected to reject code");
            }
        }
    }

    TEST_METHOD(StructuredOutputGarbageCommentTest)
    {
        HRESULT hr = E_FAIL;

        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        for (WCHAR x = 0x01; x < 0xFFFF; x++)
        {
            auto result_stream = std::make_shared<MemoryStream>(_L_);
            Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));
            hr = WriteGargabeCommentTest(result_stream, x);
            // if (SUCCEEDED(hr))
            //{
            //	log::Info(L, L"{ /* %.4X */ true},", x);
            //}
            // else
            //{
            //	log::Info(L, L"{ /* %.4X */ false},", x);
            //}

            if (IsUnicodeValid(xml_comment_table, x))
            {
                Assert::IsTrue(SUCCEEDED(hr), L"XMLWriter test was expected to accept code");
            }
            else
            {
                Assert::IsTrue(FAILED(hr), L"XMLWriter test was expected to reject code");
            }
        }
    }

    TEST_METHOD(StructuredOutputSanitizedTest)
    {
        HRESULT hr = E_FAIL;

        auto xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

        auto result_stream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(result_stream->OpenForReadWrite()));

        Assert::IsTrue(SUCCEEDED(WriteSanitizedTest(result_stream)));
        Assert::IsTrue(SUCCEEDED(CompareTestResult(result_stream, L"3FAB138DB1BB32154972E28C3128A4DC1B78EB94")));
    }

    TEST_METHOD(JSONStructuredOutput)
    {

        auto stream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(stream->OpenForReadWrite()));

        auto writer = StructuredOutput::JSON::GetWriter(_L_, stream, nullptr);

        writer->BeginElement(L"element");
        writer->WriteNamed(L"name", L"value");
        writer->WriteNamed(L"integer", 123U);
        writer->WriteNamed(L"integer_hex", 123U, true);
        writer->WriteNamed(L"longlong", 23423432434123LU);
        writer->WriteNamed(L"longlong_hex", 23423432434123LU, true);

        LARGE_INTEGER li;
        li.QuadPart = 23423432434123LU;
        writer->WriteNamed(L"largeint", 23423432434123LU);
        writer->WriteNamed(L"largeint_hex", 23423432434123LU, true);

        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        writer->WriteNamed(L"system_time", ft);

        writer->EndElement(L"element");

        writer->BeginCollection(L"items");
        for (int i = 0; i<10; ++i)
        {
            writer->BeginElement(nullptr);
            writer->WriteNamed(L"integer", 123U);
            writer->EndElement(nullptr);
        }
        writer->EndCollection(L"items");

        writer->Close();
        stream;

    }
};
}  // namespace Orc::Test
