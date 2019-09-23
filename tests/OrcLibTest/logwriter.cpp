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

#include "LogFileWriter.h"

using namespace std;

using namespace std::string_literals;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(LogFileWriterTest)
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

    class TestFriend
    {

    private:
        int m_recurseLevel = 0L;

        std::wstring m_strString = L"some test string"s;
        LONG m_lLong = 34L;

        static LogFileWriter& level(LogFileWriter& L, TestFriend& test, int RecurseLevel)
        {
            test.m_recurseLevel = RecurseLevel;
        }

        friend Orc::logger& operator<<(Orc::logger& L, const TestFriend& obj)
        {
            L->WriteString(obj.m_strString);
            L->WriteBytesInHex((DWORD)obj.m_lLong, true);
            L->WriteFormatedString(L"(%d)", obj.m_recurseLevel);
            return L;
        }
    };

    TEST_METHOD(LogWriterBasicTest)
    {
        _L_->WriteString(L"Hello world!!\r\n");

        log::Info(_L_, L"Test avec accents: éèêëàâäùiïîç\r\n");
        log::Info(_L_, L"Test avec accents: éèêëàâäùiïîç\r\n");
        log::Error(_L_, E_FAIL, L"This is an error message...\r\n");
        log::Warning(_L_, E_FAIL, L"Same as a warning\r\n");
    }

    TEST_METHOD(LogWriterStreamTest)
    {
        _L_ << L"Bonjour le monde!!" << log::endl;

        _L_ << "log in hex: " << log::hex << (ULONG)12345L << log::dec << log::endl;

        SYSTEMTIME st;
        GetSystemTime(&st);
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        _L_ << "Current system time: " << ft << log::endl;

        LPCWSTR szTest = L"This is a test";
        CBinaryBuffer buffer;
        buffer.SetData((LPBYTE)szTest, (wcslen(szTest) + 1) * sizeof(WCHAR));

        _L_ << L"Test in hex: " << log::hex_0x << buffer << log::dec << log::endl;

        auto backupFormat = _L_->IntFormat();

        _L_ << L"Test format: " << log::hex << 12L << L" " << log::dec << 12L << log::format(backupFormat) << log::endl;

        _L_ << L"log one byte in decimal: " << log::dec << (BYTE)45 << log::endl;
        _L_ << L"log one byte in hex: " << log::hex_0x << (BYTE)45 << log::endl;

        _L_ << L"log one short in decimal: " << log::dec << (SHORT)45 << log::endl;
        _L_ << L"log one short in hex: " << log::hex_0x << (SHORT)45 << log::endl;

        _L_ << L"log in decimal: " << log::dec << (ULONG)12345L << log::endl;

        _L_ << L"long long in decimal: " << log::dec << 12345678945613213465ULL << log::endl;
        _L_ << L"long long in hex: " << log::hex_0x << 12345678945613213465ULL << log::endl;

        std::vector<BYTE> vect_bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        _L_ << "Log one vector of bytes: " << vect_bytes << log::endl;

        std::vector<DWORD> vect_dword = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        _L_ << "Log one vector of DWORD: " << vect_dword << log::endl;

        // testing multiple new lines
        _L_ << L"3 new lines below" << log::endl;

        TestFriend t;

        _L_ << "testing friend operator: " << t << log::endl;

        // L << "file time" << log::filetime << log::endl;

        return;
    }
};
}  // namespace Orc::Test
