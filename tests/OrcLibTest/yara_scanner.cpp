//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "LogFileWriter.h"
#include "YaraScanner.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

using namespace std::string_literals;

namespace Orc::Test {
TEST_CLASS(YaraScannerTest)
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

    TEST_METHOD(SimpleScan)
    {
        YaraScanner scanner(_L_);

        Assert::IsTrue(SUCCEEDED(scanner.Initialize()));

        Assert::IsTrue(SUCCEEDED(scanner.Configure(std::unique_ptr<YaraConfig>())));

        auto rules = R"(
				import "pe"
				import "hash"
				/*
				This is a simple string lookup rule ...
				*/
				rule simple_string{
					strings:
						$text_string = "HelloWorld"
					condition :
						$text_string
				}
				rule single_section
				{
				condition:
					pe.number_of_sections == 1
				}

				rule control_panel_applet
				{
				condition:
					pe.exports("CPlApplet")
				}

				rule is_dll
				{
				condition:
					pe.characteristics & pe.DLL
				}
			)"s;

        {
            CBinaryBuffer buffer;
            buffer.SetData((LPBYTE)rules.c_str(), rules.size());
            Assert::IsTrue(SUCCEEDED(scanner.AddRules(buffer)));
        }

        {
            auto strText = "This is a text with HelloWorld inside it"s;
            CBinaryBuffer buffer;
            buffer.SetData((LPBYTE)strText.c_str(), strText.size());

            {
                MatchingRuleCollection matchingRules;
                Assert::IsTrue(SUCCEEDED(scanner.Scan(buffer, matchingRules)));
                Assert::AreEqual(matchingRules.size(), static_cast<size_t>(1), L"we expect exactly one rule to match");
            }

            {
                scanner.DisableRule("simple_string");
                MatchingRuleCollection matchingRules;
                Assert::IsTrue(SUCCEEDED(scanner.Scan(buffer, matchingRules)));
                Assert::AreEqual(matchingRules.size(), static_cast<size_t>(0), L"we expect exactly no rule to match");
            }

            {
                scanner.EnableRule("simple_string");
                MatchingRuleCollection matchingRules;
                Assert::IsTrue(SUCCEEDED(scanner.Scan(buffer, matchingRules)));
                Assert::AreEqual(matchingRules.size(), static_cast<size_t>(1), L"we expect exactly no rule to match");
            }
        }
    }
};
}  // namespace Orc::Test