//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"
#include "Buffer.h"
#include "Registry.h"

using namespace std;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
    TEST_CLASS(RegistryTest)
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


        TEST_METHOD(ReadKeyNotFound)
        {
            auto BuildBranch = Registry::Read<std::wstring>(
                _L_, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\UnknownVersion)", L"BuildBranch");
            Assert::IsTrue(BuildBranch.is_err());
            Assert::IsTrue(BuildBranch.err_value() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        }
        TEST_METHOD(ReadValueNotFound)
        {
            auto BuildBranch = Registry::Read<std::wstring>(
                _L_, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"UnknownBranch");
            Assert::IsTrue(BuildBranch.is_err());
            Assert::IsTrue(BuildBranch.err_value() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        }
        TEST_METHOD(ReadString)
        {
            auto BuildBranch = Registry::Read<std::wstring>(
                _L_, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"BuildBranch");

            if (BuildBranch.is_err())
            {
                log::Error(_L_, BuildBranch.err_value(), L"Failed to read BuildBranch, test not performed\r\n");
                return;
            }

            Assert::IsTrue(BuildBranch.is_ok());
            Assert::IsFalse(move(BuildBranch).unwrap().empty());
        }

        TEST_METHOD(ReadDWORD)
        {
            auto VersionNumber = Registry::Read<ULONG32>(
                _L_,
                HKEY_LOCAL_MACHINE,
                LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
                L"CurrentMajorVersionNumber");

            if (VersionNumber.is_err())
            {
                log::Error(_L_, VersionNumber.err_value(), L"Failed to read VersionNumber, test not performed\r\n");
                return;
            }

            Assert::IsTrue(VersionNumber.is_ok());
            Assert::IsTrue(move(VersionNumber).unwrap() > 5);
        }
        TEST_METHOD(ReadQWORD)
        {
            auto InstallTime = Registry::Read<ULONG64>(
                _L_,
                HKEY_LOCAL_MACHINE,
                LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
                L"InstallTime");

            if (InstallTime.is_err())
            {
                log::Error(_L_, InstallTime.err_value(), L"Failed to read InstallTime, test not performed\r\n");
                return;
            }

            Assert::IsTrue(InstallTime.is_ok());
            ULARGE_INTEGER li;
            li.QuadPart = move(InstallTime).unwrap();

            FILETIME ft;
            ft.dwLowDateTime = li.LowPart;
            ft.dwHighDateTime = li.HighPart;

            SYSTEMTIME st;
            FileTimeToSystemTime(&ft, &st);
            Assert::IsTrue(st.wYear > 2000 && st.wYear < 2500);
        }
        TEST_METHOD(ReadPath)
        {
            auto PathName = Registry::Read<std::filesystem::path>(
                _L_, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"PathName");
            if (PathName.is_err())
            {
                log::Error(_L_, PathName.err_value(), L"Failed to read PathName, test not performed\r\n");
                return;
            }

            Assert::IsTrue(PathName.is_ok());
            Assert::IsTrue(std::filesystem::exists(move(PathName).unwrap()));
        }
        TEST_METHOD(ReadBinary)
        {
            auto DigitalProductId = Registry::Read<ByteBuffer>(
                _L_, HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"DigitalProductId4");

            if(DigitalProductId.is_err())
            {
                log::Error(_L_, DigitalProductId.err_value(), L"Failed to read DigitalProductId4, test not performed\r\n");
                return;
            }
            Assert::IsTrue(DigitalProductId.is_ok());
            auto buffer = move(DigitalProductId).unwrap();
            Assert::IsTrue(buffer.size() > 0);
        }
    };
}

