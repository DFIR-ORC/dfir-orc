//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <memory>
#include <iostream>
#include <iomanip>

#include "Convert.h"

using namespace std;

using namespace std::string_literals;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(Convert)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
    }

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(IntegerConvert)
    {
        // Same Sizes
        Assert::IsTrue(Orc::ConvertTo<char>((char)127) == (char)127);
        Assert::IsTrue(Orc::ConvertTo<char>((char)-127) == (char)-127);
        Assert::IsTrue(Orc::ConvertTo<unsigned char>((char)127) == (char)127);
        Assert::IsTrue(Orc::ConvertTo<char>((unsigned char)127) == (char)127);
        Assert::ExpectException<msl::utilities::SafeIntException&>([]() { Orc::ConvertTo<unsigned char>((char)-127); });
        Assert::ExpectException<msl::utilities::SafeIntException&>([]() { Orc::ConvertTo<char>((unsigned char)128); });

        // Destination is bigger than source
        Assert::IsTrue(Orc::ConvertTo<unsigned int>((char)127) == 127U);
        Assert::IsTrue(Orc::ConvertTo<int>((unsigned char)127) == 127);
        Assert::IsTrue(Orc::ConvertTo<int>((char)127) == 127);
        Assert::IsTrue(Orc::ConvertTo<int>((char)-127) == -127);
        Assert::ExpectException<msl::utilities::SafeIntException&>([]() { Orc::ConvertTo<unsigned int>((char)-127); });

        // Source is bigger than destination
        Assert::IsTrue(Orc::ConvertTo<unsigned char>(127U) == (unsigned char)127);
        Assert::IsTrue(Orc::ConvertTo<char>(127) == (char)127);
        Assert::IsTrue(Orc::ConvertTo<char>(-127) == (char)-127);
        Assert::IsTrue(Orc::ConvertTo<unsigned char>(255) == (unsigned char)255);
        Assert::ExpectException<msl::utilities::SafeIntException&>([]() { Orc::ConvertTo<unsigned char>(256); });
        Assert::ExpectException<msl::utilities::SafeIntException&>([]() { Orc::ConvertTo<unsigned char>(-127LL); });
        Assert::ExpectException<msl::utilities::SafeIntException&>([]() { Orc::ConvertTo<char>(-130LL); });
    }
    TEST_METHOD(Base64Convert)
    {
        using namespace std::string_literals;
        const auto GMT = L"VFppZgAAAAAAAAAAAAAAAAAAAAAAAAABAAAAAQAAAAAAAAAAAAAAAQAAAAQAAAAAAABHTVQAAAA="s;

        auto binary = Orc::ConvertBase64(GMT);

        Assert::AreEqual(binary.size(), (ULONG)0x38, L"BASE64 conversion result does not have the expected result");

        const auto strGMT = Orc::ConvertToBase64(binary);
        Assert::AreEqual(strGMT, GMT, L"Double BASE64 conversion does not have the expected result");

        const auto UTC =
            L"VFppZjIAAAAAAAAAAAAAAAAAAAAAAAABAAAAAQAAAAAAAAAAAAAAAQAAAAQAAAAA"
            L"AABVVEMAAABUWmlmMgAAAAAAAAAAAAAAAAAAAAAAAAEAAAABAAAAAAAAAAEAAAAB"
            L"AAAABPgAAAAAAAAAAAAAAAAAAFVUQwAAAApVVEMwCg=="s;

        auto binary_utc = Orc::ConvertBase64(UTC);
        Assert::AreEqual(binary_utc.size(), (ULONG)0x7F, L"BASE64 conversion result does not have the expected result");

        const auto strUTC = Orc::ConvertToBase64(binary_utc);
        Assert::AreEqual(strUTC, UTC, L"Double BASE64 conversion does not have the expected result");
    }
};
}  // namespace Orc::Test
