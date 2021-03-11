//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "OrcException.h"

#include <string_view>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {

    using namespace std::string_view_literals;

    TEST_CLASS(exception)
    {
    private:
        UnitTestHelper helper;

    public:
        TEST_METHOD_INITIALIZE(Initialize)
        {
        }
        TEST_METHOD_CLEANUP(Finalize) {
        }

        TEST_METHOD(BasicCatch)
        {
            Assert::ExpectException<Orc::Exception>(
                [] { throw Exception(Severity::Continue); });

            try
            {
                throw Exception(Severity::Continue, L"This is a dummy test exception");
            }
            catch (const Exception& e)
            {
                if (e.m_severity != Severity::Continue)
                    throw;

                Assert::AreEqual("Exception: This is a dummy test exception", e.what(), L"what() did not return the exepected description");
            }
        }
        TEST_METHOD(FormatDescription)
        {

            try
            {
                throw Exception(Severity::Continue, L"This is a {} test exception"sv, L"dummy"sv);
            }
            catch (const Exception& e)
            {
                if (e.m_severity != Severity::Continue)
                    throw;

                Assert::AreEqual(
                    "Exception: This is a dummy test exception",
                    e.what(), L"what() did not return the exepected description");
            }

        }
    };


}  // namespace Orc::Test

