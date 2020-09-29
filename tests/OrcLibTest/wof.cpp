//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "Partition.h"
#include "FileStream.h"
#include "MemoryStream.h"

#include "CompressAPIExtension.h"

#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

using namespace std::filesystem;
using namespace std::string_literals;

namespace Orc::Test {
TEST_CLASS(WindowsOverlayFile)
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

    TEST_METHOD(BasicCompressionAPI)
    {
        auto compressAPI = ExtensionLibrary::GetLibrary<CompressAPIExtension>(_L_);
        // This is not related to cabinet archive format but on unfinished work about WOF support
        Assert::IsTrue((bool)compressAPI, L"Could not compression API cabinet.dll");

        CBinaryBuffer buffer;
        std::wstring strMessage = L"Hello World!!"s;

        {
            COMPRESSOR_HANDLE compressor = INVALID_HANDLE_VALUE;
            Assert::IsTrue(
                !compressAPI->CreateCompressor(COMPRESS_ALGORITHM_XPRESS, nullptr, &compressor),
                L"Compressor creation failed");

            SIZE_T requiredSize = 0L;
            Assert::IsFalse(!compressAPI->Compress(
                compressor, (PVOID)strMessage.data(), strMessage.size() * sizeof(WCHAR), nullptr, 0L, &requiredSize));
            Assert::IsTrue(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
            Assert::IsTrue(requiredSize > 0, L"Invalid, null compressed data size");

            buffer.SetCount(requiredSize);
            Assert::IsTrue(!compressAPI->Compress(
                compressor,
                (PVOID)strMessage.c_str(),
                (strMessage.size() + 1) * sizeof(WCHAR),
                (PVOID)buffer.GetData(),
                buffer.GetCount(),
                nullptr));

            Assert::IsTrue(!compressAPI->CloseCompressor(compressor));
        }
        {
            COMPRESSOR_HANDLE decompressor = INVALID_HANDLE_VALUE;
            Assert::IsTrue(!compressAPI->CreateDecompressor(COMPRESS_ALGORITHM_XPRESS, nullptr, &decompressor));

            SIZE_T requiredSize = 0L;
            Assert::IsFalse(!compressAPI->Decompress(
                decompressor, (PVOID)buffer.GetData(), (SIZE_T)buffer.GetCount(), nullptr, 0L, &requiredSize));
            Assert::IsTrue(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
            Assert::IsTrue(requiredSize > 0, L"Invalid, null compressed data size");

            CBinaryBuffer expandBuffer;
            expandBuffer.SetCount(requiredSize);
            Assert::IsTrue(!compressAPI->Decompress(
                decompressor,
                (PVOID)buffer.GetData(),
                (SIZE_T)buffer.GetCount(),
                (PVOID)expandBuffer.GetData(),
                (SIZE_T)expandBuffer.GetCount(),
                nullptr));

            Assert::IsTrue(strMessage._Equal(expandBuffer.GetP<WCHAR>()), L"Note the expected message");
        }
    }
#ifdef BASIC_WOLF_DECOMPRESSION
    TEST_METHOD(BasicWofDecompression)
    {
        auto sample =
            std::filesystem::path(helper.GetDirectoryName(__WFILE__) + L"\\binaries\\vcpkg.exe.WofCompressedData");

        Assert::IsTrue(exists(sample), L"Sample file \\binaries\\vcpkg.exe.WofCompressedData does not exist");

        auto wofStream = std::make_shared<FileStream>(_L_);
        Assert::IsTrue(SUCCEEDED(wofStream->ReadFrom(sample.c_str())));

        ULONGLONG wofSize = wofStream->GetSize();

        auto compressedStream = std::make_shared<MemoryStream>(_L_);
        Assert::IsTrue(SUCCEEDED(compressedStream->OpenForReadWrite()));

        ULONGLONG compressedSize = 0LLU;
        Assert::IsTrue(SUCCEEDED(wofStream->CopyTo(compressedStream, &compressedSize)));
        Assert::IsTrue(SUCCEEDED(wofStream->Close()));

        Assert::AreEqual(compressedSize, wofSize);
        const auto compressedData = compressedStream->GetConstBuffer();

        auto compressAPI = ExtensionLibrary::GetLibrary<CompressAPIExtension>(_L_);
        COMPRESSOR_HANDLE decompressor = INVALID_HANDLE_VALUE;
        Assert::IsTrue(!compressAPI->CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &decompressor));

        SIZE_T bytesNeeded = 0L;
        HRESULT hr = E_FAIL;

        if (auto hr = compressAPI->Decompress(
                decompressor,
                (PVOID)compressedData.GetData(),
                (SIZE_T)compressedData.GetCount(),
                nullptr,
                0L,
                &bytesNeeded);
            FAILED(hr))
            Assert::IsFalse(SUCCEEDED(hr));

        Assert::IsTrue(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

        CBinaryBuffer expandedBytes;
        expandedBytes.SetCount(bytesNeeded);

        Assert::IsTrue(SUCCEEDED(compressAPI->Decompress(
            decompressor,
            (PVOID)compressedData.GetData(),
            (SIZE_T)compressedData.GetCount(),
            (PVOID)expandedBytes.GetData(),
            (SIZE_T)expandedBytes.GetCount(),
            nullptr)));
    };

#endif  // BASIC_WOLF_DECOMPRESSION
};
}  // namespace Orc::Test