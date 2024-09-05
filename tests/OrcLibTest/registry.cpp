//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

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
        UnitTestHelper helper;

    public:
        TEST_METHOD_INITIALIZE(Initialize)
        {
        }

        TEST_METHOD_CLEANUP(Finalize) {}


        TEST_METHOD(ReadKeyNotFound)
        {
            auto BuildBranch = Registry::Read<std::wstring>(
                HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\UnknownVersion)", L"BuildBranch");
            Assert::IsTrue(BuildBranch.has_error());
            Assert::IsTrue(BuildBranch.error().value() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        }
        TEST_METHOD(ReadValueNotFound)
        {
            auto BuildBranch = Registry::Read<std::wstring>(
                HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"UnknownBranch");
            Assert::IsTrue(BuildBranch.has_error());
            Assert::IsTrue(BuildBranch.error().value() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        }
        TEST_METHOD(ReadString)
        {
            auto BuildBranch = Registry::Read<std::wstring>(
                HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"BuildBranch");

            if (BuildBranch.has_error())
            {
                Log::Error(L"Failed to read BuildBranch, test not performed (code: {:#x})", BuildBranch.error().value());
                return;
            }

            Assert::IsTrue(BuildBranch.has_value());
            Assert::IsFalse(BuildBranch.value().empty());
        }

        TEST_METHOD(ReadDWORD)
        {
            auto VersionNumber = Registry::Read<ULONG32>(
                HKEY_LOCAL_MACHINE,
                LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
                L"CurrentMajorVersionNumber");

            if (VersionNumber.has_error())
            {
                Log::Error(
                    L"Failed to read VersionNumber, test not performed (code: {:#x})", VersionNumber.error().value());
                return;
            }

            Assert::IsTrue(VersionNumber.has_value());
            Assert::IsTrue(VersionNumber.value() > 5);
        }
        TEST_METHOD(ReadQWORD)
        {
            auto InstallTime = Registry::Read<ULONG64>(
                HKEY_LOCAL_MACHINE,
                LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
                L"InstallTime");

            if (InstallTime.has_error())
            {
                Log::Error(L"Failed to read InstallTime, test not performed (code: {:#x})", InstallTime.error().value());
                return;
            }

            Assert::IsTrue(InstallTime.has_value());
            ULARGE_INTEGER li;
            li.QuadPart = *InstallTime;

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
                HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"PathName");
            if (PathName.has_error())
            {
                Log::Error(L"Failed to read PathName, test not performed (code: {:#x})", PathName.error().value());
                return;
            }

            Assert::IsTrue(PathName.has_value());
            Assert::IsTrue(std::filesystem::exists(PathName.value()));
        }
        TEST_METHOD(ReadBinary)
        {
            auto DigitalProductId = Registry::Read<ByteBuffer>(
                HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)", L"DigitalProductId4");

            if(DigitalProductId.has_error())
            {
                Log::Error(
                    L"Failed to read DigitalProductId4, test not performed (code: {:#x})",
                    DigitalProductId.error().value());
                return;
            }
            Assert::IsTrue(DigitalProductId.has_value());
            auto& buffer = *DigitalProductId;
            Assert::IsTrue(buffer.size() > 0);
        }
    };
}
