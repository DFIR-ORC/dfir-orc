//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "FuzzyHashStream.h"
#include "MemoryStream.h"
#include "FileStream.h"
#include "DevNullStream.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(HashStreamTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(FuzzyHashStreamBasicTest)
    {
        auto filestream = std::make_shared<FileStream>();

        auto test_file_path = helper.GetDirectoryName(__WFILE__) + L"\\binaries\\ntfsinfo.exe";
        Assert::IsTrue(SUCCEEDED(filestream->ReadFrom(test_file_path.c_str())));

        auto fuzzy_stream = std::make_unique<FuzzyHashStream>();

        Assert::IsTrue(SUCCEEDED(fuzzy_stream->OpenToRead(FuzzyHashStream::Algorithm::SSDeep, filestream)));

        auto devnull = std::make_shared<DevNullStream>();
        Assert::IsTrue(SUCCEEDED(devnull->Open()));

        ULONGLONG ullBytesCopied = 0LL;
        Assert::IsTrue(SUCCEEDED(fuzzy_stream->CopyTo(devnull, &ullBytesCopied)));
        Assert::IsTrue(ullBytesCopied == 139432, L"Size mismatch in ntfsinfo.exe");

        Assert::IsTrue(SUCCEEDED(fuzzy_stream->Close()));

#ifdef ORC_BUILD_SSDEEP
        std::wstring ssdeep;
        Assert::IsTrue(SUCCEEDED(fuzzy_stream->GetHash(FuzzyHashStream::Algorithm::SSDeep, ssdeep)));
        Assert::AreEqual(
            L"3072:LTA1oiyclh4NWZUFy13JwjhwDmBc6hZ/Eg:OyuKbycWa",
            ssdeep.c_str(),
            L"SSDeep value for ntfsinfo.exe does not match expected value");
#endif  // ORC_BUILD_SSDEEP

        return;
    }
};
}  // namespace Orc::Test
