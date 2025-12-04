//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ParameterCheck.h"
#include <limits>
#include <errno.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(ParameterCheckTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
        // Reset errno before each test
        errno = 0;
    }

    TEST_METHOD_CLEANUP(Finalize)
    {
        // Reset errno after each test
        errno = 0;
    }

    // ===== GetIntegerFromArg (LARGE_INTEGER, WCHAR*) Tests =====

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_ValidPositive)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"12345", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(12345LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_ValidNegative)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"-9876", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(-9876LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_ValidHex)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"0xFF", result, 16);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(255LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_ValidOctal)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"777", result, 8);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(511LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_WithLeadingSpaces)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"  42", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(42LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_WithTrailingSpaces)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"42  ", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(42LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_InvalidNull)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg((const WCHAR*)nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_InvalidEmpty)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_InvalidNonNumeric)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"abc", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_InvalidTrailingText)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg(L"123abc", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_Overflow)
    {
        LARGE_INTEGER result;
        // Try a value that will overflow long long
        HRESULT hr = GetIntegerFromArg(L"99999999999999999999999999999", result);
        Assert::IsTrue(FAILED(hr));
    }

    // Test errno resilience by mixing valid and invalid calls
    TEST_METHOD(GetIntegerFromArg_LargeInteger_WChar_ErrnoResilience)
    {
        LARGE_INTEGER result;

        // Invalid call - sets errno
        HRESULT hr1 = GetIntegerFromArg(L"99999999999999999999999999999", result);
        Assert::IsTrue(FAILED(hr1));

        // Valid call - should succeed despite previous errno
        HRESULT hr2 = GetIntegerFromArg(L"42", result);
        Assert::IsTrue(SUCCEEDED(hr2));
        Assert::AreEqual(42LL, result.QuadPart);

        // Another invalid call
        HRESULT hr3 = GetIntegerFromArg(L"invalid", result);
        Assert::IsTrue(FAILED(hr3));

        // Another valid call
        HRESULT hr4 = GetIntegerFromArg(L"-100", result);
        Assert::IsTrue(SUCCEEDED(hr4));
        Assert::AreEqual(-100LL, result.QuadPart);
    }

    // ===== GetIntegerFromArg (LARGE_INTEGER, CHAR*) Tests =====

    TEST_METHOD(GetIntegerFromArg_LargeInteger_Char_ValidPositive)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg("54321", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(54321LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_Char_ValidNegative)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg("-1234", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(-1234LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_Char_InvalidNull)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg((const CHAR*)nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_Char_InvalidTrailingText)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromArg("456xyz", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_LargeInteger_Char_ErrnoResilience)
    {
        LARGE_INTEGER result;

        // Invalid, valid, invalid, valid pattern
        Assert::IsTrue(FAILED(GetIntegerFromArg("overflow99999999999999999999", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg("1000", result)));
        Assert::AreEqual(1000LL, result.QuadPart);
        Assert::IsTrue(FAILED(GetIntegerFromArg("notanumber", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg("-500", result)));
        Assert::AreEqual(-500LL, result.QuadPart);
    }

    // ===== GetIntegerFromArg (DWORD, WCHAR*) Tests =====

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_ValidPositive)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg(L"12345", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(12345UL, result);
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_ValidHex)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg(L"ABCD", result, 16);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(0xABCDUL, result);
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_InvalidZero)
    {
        DWORD result;
        // Zero is treated as invalid by GetIntegerFromArg for DWORD
        HRESULT hr = GetIntegerFromArg(L"0", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_InvalidNull)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg((const WCHAR*)nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_InvalidNonNumeric)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg(L"xyz", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_Overflow)
    {
        DWORD result;
        // Value larger than DWORD max
        HRESULT hr = GetIntegerFromArg(L"99999999999", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_WChar_ErrnoResilience)
    {
        DWORD result;

        // Mix invalid and valid calls
        Assert::IsTrue(FAILED(GetIntegerFromArg(L"0", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"100", result)));
        Assert::AreEqual(100UL, result);
        Assert::IsTrue(FAILED(GetIntegerFromArg(L"invalid", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"255", result)));
        Assert::AreEqual(255UL, result);
        Assert::IsTrue(FAILED(GetIntegerFromArg(L"99999999999999", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"1", result)));
        Assert::AreEqual(1UL, result);
    }

    // ===== GetIntegerFromArg (DWORD, CHAR*) Tests =====

    TEST_METHOD(GetIntegerFromArg_DWORD_Char_ValidPositive)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg("9999", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(9999UL, result);
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_Char_InvalidZero)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg("0", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_Char_InvalidNull)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromArg((const CHAR*)nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetIntegerFromArg_DWORD_Char_ErrnoResilience)
    {
        DWORD result;

        Assert::IsTrue(FAILED(GetIntegerFromArg("0", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg("50", result)));
        Assert::AreEqual(50UL, result);
        Assert::IsTrue(FAILED(GetIntegerFromArg("text", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg("200", result)));
        Assert::AreEqual(200UL, result);
    }

    // ===== GetIntegerFromArg (size_t, WCHAR*) Tests =====

    TEST_METHOD(GetIntegerFromArg_SizeT_ValidPositive)
    {
        size_t result;
        HRESULT hr = GetIntegerFromArg(L"65536", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual((size_t)65536, result);
    }

    TEST_METHOD(GetIntegerFromArg_SizeT_InvalidZero)
    {
        size_t result;
        // Zero is treated as invalid for size_t
        HRESULT hr = GetIntegerFromArg(L"0", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromArg_SizeT_InvalidNull)
    {
        size_t result;
        HRESULT hr = GetIntegerFromArg((const WCHAR*)nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetIntegerFromArg_SizeT_ValidHex)
    {
        size_t result;
        HRESULT hr = GetIntegerFromArg(L"1000", result, 16);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual((size_t)0x1000, result);
    }

    TEST_METHOD(GetIntegerFromArg_SizeT_ErrnoResilience)
    {
        size_t result;

        Assert::IsTrue(FAILED(GetIntegerFromArg(L"0", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"1024", result)));
        Assert::AreEqual((size_t)1024, result);
        Assert::IsTrue(FAILED(GetIntegerFromArg(L"bad", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"2048", result)));
        Assert::AreEqual((size_t)2048, result);
    }

    // ===== GetIntegerFromHexaString Tests =====

    TEST_METHOD(GetIntegerFromHexaString_DWORD_WChar_Valid)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromHexaString(L"FF", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(0xFFUL, result);
    }

    TEST_METHOD(GetIntegerFromHexaString_DWORD_WChar_ValidWithPrefix)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromHexaString(L"0xABCD", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(0xABCDUL, result);
    }

    TEST_METHOD(GetIntegerFromHexaString_DWORD_WChar_InvalidZero)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromHexaString(L"0", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetIntegerFromHexaString_DWORD_Char_Valid)
    {
        DWORD result;
        HRESULT hr = GetIntegerFromHexaString("CAFE", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(0xCAFEUL, result);
    }

    TEST_METHOD(GetIntegerFromHexaString_LargeInteger_WChar_Valid)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromHexaString(L"DEADBEEF", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(0xDEADBEEFLL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromHexaString_LargeInteger_Char_Valid)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetIntegerFromHexaString("1234", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(0x1234LL, result.QuadPart);
    }

    TEST_METHOD(GetIntegerFromHexaString_ErrnoResilience)
    {
        DWORD result;

        Assert::IsTrue(FAILED(GetIntegerFromHexaString(L"0", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromHexaString(L"FF", result)));
        Assert::AreEqual(0xFFUL, result);
        Assert::IsTrue(FAILED(GetIntegerFromHexaString(L"GGGG", result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromHexaString(L"0xAB", result)));
        Assert::AreEqual(0xABUL, result);
    }

    // ===== GetPercentageFromArg Tests =====

    TEST_METHOD(GetPercentageFromArg_Valid)
    {
        DWORD result;
        HRESULT hr = GetPercentageFromArg(L"50%", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(50UL, result);
    }

    TEST_METHOD(GetPercentageFromArg_ValidWithoutPercent)
    {
        DWORD result;
        HRESULT hr = GetPercentageFromArg(L"75", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(75UL, result);
    }

    TEST_METHOD(GetPercentageFromArg_ValidWithSpaces)
    {
        DWORD result;
        HRESULT hr = GetPercentageFromArg(L"  25%  ", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(25UL, result);
    }

    TEST_METHOD(GetPercentageFromArg_InvalidZero)
    {
        DWORD result;
        // Zero is invalid for percentage
        HRESULT hr = GetPercentageFromArg(L"0%", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetPercentageFromArg_InvalidOverHundred)
    {
        DWORD result;
        HRESULT hr = GetPercentageFromArg(L"101%", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetPercentageFromArg_InvalidNull)
    {
        DWORD result;
        HRESULT hr = GetPercentageFromArg(nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetPercentageFromArg_InvalidNonNumeric)
    {
        DWORD result;
        HRESULT hr = GetPercentageFromArg(L"abc%", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetPercentageFromArg_ErrnoResilience)
    {
        DWORD result;

        Assert::IsTrue(FAILED(GetPercentageFromArg(L"0%", result)));
        Assert::IsTrue(SUCCEEDED(GetPercentageFromArg(L"10%", result)));
        Assert::AreEqual(10UL, result);
        Assert::IsTrue(FAILED(GetPercentageFromArg(L"150%", result)));
        Assert::IsTrue(SUCCEEDED(GetPercentageFromArg(L"50", result)));
        Assert::AreEqual(50UL, result);
        Assert::IsTrue(FAILED(GetPercentageFromArg(L"invalid", result)));
        Assert::IsTrue(SUCCEEDED(GetPercentageFromArg(L"100%", result)));
        Assert::AreEqual(100UL, result);
    }

    // ===== GetFileSizeFromArg Tests =====

    TEST_METHOD(GetFileSizeFromArg_ValidBytes)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"1024", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(1024LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidKilobytes)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"10K", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(10240LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidKilobytesLowercase)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"10k", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(10240LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidKilobytesWithB)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"5KB", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(5120LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidMegabytes)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"2M", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(2097152LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidMegabytesWithB)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"3MB", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(3145728LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidGigabytes)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"1G", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(1073741824LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidGigabytesWithB)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"2GB", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(2147483648LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidByteSuffix)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"512B", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(512LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidWithSpaces)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"  100 KB  ", result);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(102400LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_ValidHex)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"0x100", result, 16);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(256LL, result.QuadPart);
    }

    TEST_METHOD(GetFileSizeFromArg_InvalidNull)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(nullptr, result);
        Assert::IsTrue(FAILED(hr));
        Assert::AreEqual(E_POINTER, hr);
    }

    TEST_METHOD(GetFileSizeFromArg_InvalidEmpty)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetFileSizeFromArg_InvalidNonNumeric)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"abc", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetFileSizeFromArg_InvalidOnlySuffix)
    {
        LARGE_INTEGER result;
        HRESULT hr = GetFileSizeFromArg(L"KB", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetFileSizeFromArg_Overflow)
    {
        LARGE_INTEGER result;
        // Try to overflow during multiplication
        HRESULT hr = GetFileSizeFromArg(L"99999999999999999GB", result);
        Assert::IsTrue(FAILED(hr));
    }

    TEST_METHOD(GetFileSizeFromArg_ErrnoResilience)
    {
        LARGE_INTEGER result;

        // Mix invalid and valid calls to test errno isolation
        Assert::IsTrue(FAILED(GetFileSizeFromArg(L"invalid", result)));
        Assert::IsTrue(SUCCEEDED(GetFileSizeFromArg(L"1024", result)));
        Assert::AreEqual(1024LL, result.QuadPart);

        Assert::IsTrue(FAILED(GetFileSizeFromArg(L"999999999999GB", result)));
        Assert::IsTrue(SUCCEEDED(GetFileSizeFromArg(L"10KB", result)));
        Assert::AreEqual(10240LL, result.QuadPart);

        Assert::IsTrue(FAILED(GetFileSizeFromArg(L"", result)));
        Assert::IsTrue(SUCCEEDED(GetFileSizeFromArg(L"2MB", result)));
        Assert::AreEqual(2097152LL, result.QuadPart);

        Assert::IsTrue(FAILED(GetFileSizeFromArg(nullptr, result)));
        Assert::IsTrue(SUCCEEDED(GetFileSizeFromArg(L"1GB", result)));
        Assert::AreEqual(1073741824LL, result.QuadPart);
    }

    // ===== Complex Mixed Tests for errno Resilience =====

    TEST_METHOD(MixedIntegerConversions_ErrnoResilience)
    {
        LARGE_INTEGER li_result;
        DWORD dw_result;
        size_t sz_result;

        // Alternate between different types and valid/invalid values
        Assert::IsTrue(FAILED(GetIntegerFromArg(L"overflow", li_result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"100", dw_result)));
        Assert::AreEqual(100UL, dw_result);

        Assert::IsTrue(FAILED(GetIntegerFromArg(L"0", dw_result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"500", sz_result)));
        Assert::AreEqual((size_t)500, sz_result);

        Assert::IsTrue(FAILED(GetIntegerFromHexaString(L"INVALID", dw_result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"-42", li_result)));
        Assert::AreEqual(-42LL, li_result.QuadPart);

        Assert::IsTrue(FAILED(GetIntegerFromArg(L"text", sz_result)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromHexaString(L"FF", dw_result)));
        Assert::AreEqual(0xFFUL, dw_result);
    }

    TEST_METHOD(MixedAllTypes_ErrnoResilience)
    {
        LARGE_INTEGER li;
        DWORD dw;
        size_t sz;

        // Test mixing all conversion types
        Assert::IsTrue(FAILED(GetPercentageFromArg(L"150%", dw)));
        Assert::IsTrue(SUCCEEDED(GetFileSizeFromArg(L"1KB", li)));
        Assert::AreEqual(1024LL, li.QuadPart);

        Assert::IsTrue(FAILED(GetIntegerFromArg(L"abc", dw)));
        Assert::IsTrue(SUCCEEDED(GetPercentageFromArg(L"50%", dw)));
        Assert::AreEqual(50UL, dw);

        Assert::IsTrue(FAILED(GetFileSizeFromArg(L"", li)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromArg(L"1000", sz)));
        Assert::AreEqual((size_t)1000, sz);

        Assert::IsTrue(FAILED(GetIntegerFromArg(L"0", sz)));
        Assert::IsTrue(SUCCEEDED(GetIntegerFromHexaString(L"0xCAFE", dw)));
        Assert::AreEqual(0xCAFEUL, dw);
    }
};
}  // namespace Orc::Test
