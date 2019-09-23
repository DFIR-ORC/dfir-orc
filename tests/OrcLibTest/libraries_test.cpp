//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "XmlLiteExtension.h"
#include "VssAPIExtension.h"
#include "Kernel32Extension.h"
#include "NtDllExtension.h"
#include "WinTrustExtension.h"

#include <thread>
#include <vector>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(LibrariesTest)
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

    TEST_METHOD(LibrariesBasicTest)
    {
        {
            const auto pXmlLiteLib = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);
            Assert::IsTrue(pXmlLiteLib != nullptr);
        }

        {
            const auto pVssLib = ExtensionLibrary::GetLibrary<VssAPIExtension>(_L_);
            Assert::IsTrue(pVssLib != nullptr);
        }

        {
            const auto pKernel32Lib = ExtensionLibrary::GetLibrary<Kernel32Extension>(_L_);
            Assert::IsTrue(pKernel32Lib != nullptr);
        }

        {
            const auto pNTDLLLib = ExtensionLibrary::GetLibrary<NtDllExtension>(_L_);
            Assert::IsTrue(pNTDLLLib != nullptr);
        }

        {
            const auto pWintrustLib = ExtensionLibrary::GetLibrary<WinTrustExtension>(_L_);
            Assert::IsTrue(pWintrustLib != nullptr);
        }

        // try to start multiple threads and load same library
        {
            std::vector<std::shared_ptr<std::thread>> vect;
            for (int i = 0; i < 50; ++i)
            {
                auto t = std::make_shared<std::thread>([this]() {
                    const auto pNTDLLLib = ExtensionLibrary::GetLibrary<NtDllExtension>(_L_);
                    Assert::IsTrue(pNTDLLLib != nullptr);

                    const auto pKernel32Lib = ExtensionLibrary::GetLibrary<Kernel32Extension>(_L_);
                    Assert::IsTrue(pKernel32Lib != nullptr);

                    const auto pVssLib = ExtensionLibrary::GetLibrary<VssAPIExtension>(_L_);
                    Assert::IsTrue(pVssLib != nullptr);

                    const auto pWintrustLib = ExtensionLibrary::GetLibrary<WinTrustExtension>(_L_);
                    Assert::IsTrue(pWintrustLib != nullptr);
                });

                vect.push_back(t);
            }

            for (auto& t : vect)
            {
                t->join();
            }
        }
    }
};
}  // namespace Orc::Test
