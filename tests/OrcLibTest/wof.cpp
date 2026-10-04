//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "Partition.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "UncompressWofStream.h"
#include "Filesystem/Ntfs/Compression/WofAlgorithm.h"
#include "Filesystem/Ntfs/Compression/WofDecompressor.h"

#include "CompressAPIExtension.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

using namespace std::filesystem;
using namespace std::string_literals;

namespace Orc::Test {
TEST_CLASS(WindowsOverlayFile)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(BasicCompressionAPI)
    {
        auto compressAPI = ExtensionLibrary::GetLibrary<CompressAPIExtension>();
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
    // Regression test for the WOF last-chunk decompression bug.
    //
    // WofCompressedData is opened as the ALLOCATED (cluster-rounded) stream, so its size is larger
    // than the valid compressed data. The last chunk's size is derived as (compressedSize -
    // lastChunkOffset); feeding it the allocated size appends trailing padding to the last chunk,
    // which breaks its decompression (STATUS_BAD_COMPRESSION_BUFFER) and prevented FastFind/GetThis
    // from scanning Compact-OS-compressed system files.
    //
    // This builds a minimal 2-chunk xpress8k stream (both chunks stored raw) followed by zero
    // padding, then checks that decompression is correct when given the VALID compressed size and
    // that the padded (allocated) size does not silently reproduce the original.
    TEST_METHOD(WofLastChunkWithPaddedAllocatedSize)
    {
        using namespace Orc::Ntfs;

        const uint32_t chunkSize = 8192;  // xpress8k
        const uint32_t lastChunkSize = 100;
        const uint64_t uncompressedSize = chunkSize + lastChunkSize;  // 8292 -> 2 chunks

        std::vector<uint8_t> original(static_cast<size_t>(uncompressedSize));
        for (size_t i = 0; i < original.size(); ++i)
        {
            original[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
        }

        // WOF layout: [uint32 offset table (1 entry = chunk0 size)][chunk0 raw][chunk1 raw]
        std::vector<uint8_t> blob;
        const uint32_t offset0 = chunkSize;  // chunk0 stored raw -> compressed size == chunk size
        blob.push_back(static_cast<uint8_t>(offset0 & 0xFF));
        blob.push_back(static_cast<uint8_t>((offset0 >> 8) & 0xFF));
        blob.push_back(static_cast<uint8_t>((offset0 >> 16) & 0xFF));
        blob.push_back(static_cast<uint8_t>((offset0 >> 24) & 0xFF));
        blob.insert(blob.end(), original.begin(), original.begin() + chunkSize);  // chunk0 raw
        blob.insert(blob.end(), original.begin() + chunkSize, original.end());  // chunk1 raw
        const uint64_t validCompressedSize = blob.size();  // 4 + 8192 + 100 = 8296

        // Simulate the cluster-rounded allocated stream by appending zero padding.
        std::vector<uint8_t> padded = blob;
        padded.resize(static_cast<size_t>(validCompressedSize) + 800, 0);

        auto makeStream = [&padded]() {
            auto s = std::make_shared<MemoryStream>();
            Assert::IsTrue(SUCCEEDED(s->OpenForReadWrite()));
            ULONGLONG written = 0;
            Assert::IsTrue(SUCCEEDED(s->Write(padded.data(), padded.size(), &written)));
            return s;
        };

        // With the VALID compressed size the last chunk is exact and decompression matches original.
        {
            auto wof = std::make_shared<UncompressWofStream>();
            Assert::IsTrue(
                SUCCEEDED(wof->Open(makeStream(), WofAlgorithm::kXpress8k, uncompressedSize, validCompressedSize)),
                L"Open with valid compressed size failed");

            std::vector<uint8_t> out(static_cast<size_t>(uncompressedSize), 0);
            ULONGLONG read = 0;
            Assert::IsTrue(SUCCEEDED(wof->Read(out.data(), out.size(), &read)), L"Read with valid size failed");
            Assert::AreEqual(uncompressedSize, read, L"Unexpected read count with valid size");
            Assert::IsTrue(out == original, L"Decompressed data does not match original (valid size)");
        }

        // Regression guard: the padded allocated size (the old behavior) must NOT reproduce the
        // original -- it inflates the last chunk with padding and fails to decompress.
        {
            auto wof = std::make_shared<UncompressWofStream>();
            Assert::IsTrue(
                SUCCEEDED(
                    wof->Open(makeStream(), WofAlgorithm::kXpress8k, uncompressedSize, static_cast<uint64_t>(padded.size()))),
                L"Open with padded size failed");

            std::vector<uint8_t> out(static_cast<size_t>(uncompressedSize), 0xCC);
            ULONGLONG read = 0;
            const HRESULT hr = wof->Read(out.data(), out.size(), &read);
            Assert::IsFalse(
                SUCCEEDED(hr) && out == original, L"Padded allocated size unexpectedly reproduced the original");
        }
    }

    // An LZX file read end to end through the public stream, which is what GetThis and FastFind do. The blob is
    // the fixture's whole WofCompressedData, so this exercises the chunk table, all seven chunks and the short
    // last one together.
    TEST_METHOD(WofLzxStreamDecodesAWholeFile)
    {
        using namespace Orc::Ntfs;

        std::ifstream fixture(
            helper.GetDirectoryName(__WFILE__) + L"\\binaries\\wof_lzx_known_plaintext.wofdata", std::ios::binary);
        const std::vector<uint8_t> blob(
            (std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
        Assert::AreEqual(size_t(2764), blob.size(), L"fixture wof_lzx_known_plaintext.wofdata is missing");

        auto raw = std::make_shared<MemoryStream>();
        Assert::IsTrue(SUCCEEDED(raw->OpenForReadWrite()));
        ULONGLONG written = 0;
        Assert::IsTrue(SUCCEEDED(raw->Write(const_cast<uint8_t*>(blob.data()), blob.size(), &written)));

        auto wof = std::make_shared<UncompressWofStream>();
        Assert::IsTrue(SUCCEEDED(wof->Open(raw, WofAlgorithm::kLzx, 200000, blob.size())));

        std::vector<uint8_t> out(200000, 0xCC);
        ULONGLONG read = 0;
        Assert::IsTrue(SUCCEEDED(wof->Read(out.data(), out.size(), &read)));
        Assert::AreEqual(ULONGLONG(200000), read);

        std::vector<uint8_t> expected(200000);
        for (size_t i = 0; i < expected.size(); ++i)
        {
            expected[i] = static_cast<uint8_t>((i * 31 + 7) % 251);
        }

        const auto mismatch = std::mismatch(out.begin(), out.end(), expected.begin());
        Assert::IsTrue(
            mismatch.first == out.end(),
            (L"decoded file differs from the plaintext at offset " + std::to_wstring(mismatch.first - out.begin()))
                .c_str());
    }

    // Review Focus 1: a chunk Microsoft chose not to compress is stored raw and signalled only by the size
    // equality. It must be copied, whichever engine is selected.
    TEST_METHOD(WofDecompressorCopiesARawChunk)
    {
        using namespace Orc::Ntfs;

        std::vector<uint8_t> chunk(4096);
        for (size_t i = 0; i < chunk.size(); ++i)
        {
            chunk[i] = static_cast<uint8_t>(i & 0xFF);
        }

        for (const WofAlgorithm algorithm : {WofAlgorithm::kXpress4k, WofAlgorithm::kLzx})
        {
            Result<WofDecompressor> decompressor = WofDecompressor::Create(algorithm);
            Assert::IsTrue(decompressor.has_value());

            std::vector<uint8_t> out(chunk.size(), 0xCC);
            std::error_code ec;
            const size_t produced =
                decompressor.value().Decompress(BufferView(chunk.data(), chunk.size()), gsl::make_span(out), ec);

            Assert::IsFalse(static_cast<bool>(ec));
            Assert::AreEqual(chunk.size(), produced);
            Assert::IsTrue(out == chunk);
        }
    }

    // 'kUnknown' has no engine and must be refused at construction, which is the only remaining way for
    // 'Create' to fail now that all four defined formats are supported.
    TEST_METHOD(WofDecompressorRefusesAnUnknownAlgorithm)
    {
        Result<Ntfs::WofDecompressor> decompressor = Ntfs::WofDecompressor::Create(Ntfs::WofAlgorithm::kUnknown);
        Assert::IsFalse(decompressor.has_value());
    }

#ifdef BASIC_WOLF_DECOMPRESSION
    TEST_METHOD(BasicWofDecompression)
    {
        auto sample =
            std::filesystem::path(helper.GetDirectoryName(__WFILE__) + L"\\binaries\\vcpkg.exe.WofCompressedData");

        Assert::IsTrue(exists(sample), L"Sample file \\binaries\\vcpkg.exe.WofCompressedData does not exist");

        auto wofStream = std::make_shared<FileStream>();
        Assert::IsTrue(SUCCEEDED(wofStream->ReadFrom(sample.c_str())));

        ULONGLONG wofSize = wofStream->GetSize();

        auto compressedStream = std::make_shared<MemoryStream>();
        Assert::IsTrue(SUCCEEDED(compressedStream->OpenForReadWrite()));

        ULONGLONG compressedSize = 0LLU;
        Assert::IsTrue(SUCCEEDED(wofStream->CopyTo(compressedStream, &compressedSize)));
        Assert::IsTrue(SUCCEEDED(wofStream->Close()));

        Assert::AreEqual(compressedSize, wofSize);
        const auto compressedData = compressedStream->GetConstBuffer();

        auto compressAPI = ExtensionLibrary::GetLibrary<CompressAPIExtension>();
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
