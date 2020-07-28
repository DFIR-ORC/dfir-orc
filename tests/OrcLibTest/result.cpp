//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "OrcResult.h"
#include "Buffer.h"
#include "CaseInsensitive.h"

#include <fmt/format.h>

template <typename FormatContext>
auto formatHR(const HRESULT hr, FormatContext& ctx)
{
    fmt::format_to(ctx.out(), L"{:#x}", (ULONG32)hr);
    return;
}



using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(ResultTest)
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

    TEST_METHOD(BasicVoid)
    {
        using namespace std::string_literals;
        using namespace std::string_view_literals;

        auto access_denied = make_hr(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));

        Assert::AreEqual(L"Access is denied"s, fmt::format(L"{:msg}"sv, access_denied));
        Assert::AreEqual(L"0x80070005"s, fmt::format(L"{:hr}"sv, access_denied));
        Assert::AreEqual(L"0x80070005:Access is denied"s, fmt::format(L"{:hrmsg}"sv, access_denied));
        Assert::AreEqual(L"0x80070005:Access is denied"s, fmt::format(L"{}"sv, access_denied));

        auto ok = make_hr(S_OK);
        Assert::AreEqual(L"S_OK"s, fmt::format(L"{:msg}"sv, ok));
    }


    TEST_METHOD(Basic)
    {
        using namespace std::string_literals;
        using namespace std::string_view_literals;

        auto access_denied = make_hr<DWORD>(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));

        Assert::AreEqual(L"Access is denied"s, fmt::format(L"{:msg}"sv, access_denied));
        Assert::AreEqual(L"0x80070005"s, fmt::format(L"{:hr}"sv, access_denied));
        Assert::AreEqual(L"0x80070005:Access is denied"s, fmt::format(L"{:hrmsg}"sv, access_denied));
        Assert::AreEqual(L"0x80070005:Access is denied"s, fmt::format(L"{}"sv, access_denied));

        auto s_ok = make_hr<DWORD>(S_OK);
        Assert::AreEqual(L"0"s, fmt::format(L"{:msg}"sv, s_ok));

        auto ok = stx::make_ok<DWORD,HRESULT>(22);
        Assert::AreEqual(L"22"s, fmt::format(L"{:msg}"sv, ok));
    }
};


}  // namespace Orc::Test

