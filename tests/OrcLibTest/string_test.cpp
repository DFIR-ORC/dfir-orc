//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "Utils/String.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// ParseDecimalDigits replaces the wcstoul/wcstoull/_wcstoui64 parses: what those accepted silently (a sign, a
// leading space, trailing characters) and what they saturated (an overflow capped at ULLONG_MAX) must now be
// refused, so a size that cannot be read is never mistaken for a number.
TEST_CLASS(StringTest)
{
public:
    TEST_METHOD(ParseDecimalDigitsReadsPlainNumbers)
    {
        Assert::AreEqual(uint64_t(0), ParseDecimalDigits(L"0").value_or(1));
        Assert::AreEqual(uint64_t(7), ParseDecimalDigits(L"7").value_or(0));
        Assert::AreEqual(uint64_t(42), ParseDecimalDigits(L"042").value_or(0));
        Assert::AreEqual(uint64_t(4294967296), ParseDecimalDigits(L"4294967296").value_or(0));
        Assert::AreEqual(UINT64_MAX, ParseDecimalDigits(L"18446744073709551615").value_or(0));
    }

    TEST_METHOD(ParseDecimalDigitsRefusesAnythingElse)
    {
        for (const wchar_t* text : {L"",
                                    L" ",
                                    L" 1",
                                    L"1 ",
                                    L"+1",
                                    L"-1",
                                    L"1.0",
                                    L"1,0",
                                    L"0x10",
                                    L"10abc",
                                    L"abc",
                                    L"1\t2"})
        {
            Assert::IsFalse(ParseDecimalDigits(text).has_value(), text);
        }
    }

    // wcstoull and _wcstoui64 returned ULLONG_MAX here and only said so through errno; from_chars refuses
    // the value outright.
    TEST_METHOD(ParseDecimalDigitsRefusesOverflow)
    {
        Assert::IsFalse(ParseDecimalDigits(L"18446744073709551616").has_value());
        Assert::IsFalse(ParseDecimalDigits(L"99999999999999999999999999").has_value());
    }

    // A caller may hand over a view into a fixed buffer rather than a null-terminated string.
    TEST_METHOD(ParseDecimalDigitsHonoursTheViewLength)
    {
        const wchar_t buffer[] = L"123456";
        Assert::AreEqual(uint64_t(123), ParseDecimalDigits(std::wstring_view(buffer, 3)).value_or(0));
        Assert::IsFalse(ParseDecimalDigits(std::wstring_view(buffer, 0)).has_value());
    }

    // EqualsAsciiCaseInsensitive replaces _wcsicmp/towlower wherever a protocol token is matched (an HTTP
    // field name, an HTML tag name). What it must keep from those: ASCII case folds. What it must NOT keep:
    // a dependence on the thread's locale, or on a terminator.
    TEST_METHOD(EqualsAsciiCaseInsensitiveFoldsAsciiCase)
    {
        Assert::IsTrue(EqualsAsciiCaseInsensitive(L"Content-Type", L"content-type"));
        Assert::IsTrue(EqualsAsciiCaseInsensitive(L"CONTENT-TYPE", L"Content-Type"));
        Assert::IsTrue(EqualsAsciiCaseInsensitive(L"", L""));
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"Content-Type", L"Content-Length"));
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"Content-Type", L"Content-Type "));
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"Content", L"Content-Type"));
    }

    // Nothing but A-Z folds. A case-insensitive compare that reached further would let a name a peer never
    // wrote match one we look for - which is exactly what a locale-driven fold does with the dotted and
    // dotless I.
    TEST_METHOD(EqualsAsciiCaseInsensitiveFoldsNothingBeyondAscii)
    {
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"İnput", L"input"));  // 'İ' U+0130, Turkish casing
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"ınput", L"Input"));  // 'ı' U+0131
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"Kb", L"kb"));  // KELVIN SIGN U+212A
        Assert::IsFalse(EqualsAsciiCaseInsensitive(L"É", L"é"));  // 'É' / 'é'
    }

    // Length-based, unlike _wcsicmp() on a c_str(): an embedded NUL is a character to compare, not the end
    // of the string.
    TEST_METHOD(EqualsAsciiCaseInsensitiveDoesNotStopAtANul)
    {
        const std::wstring_view smuggled(L"Content-Type\0junk", 17);
        Assert::IsFalse(EqualsAsciiCaseInsensitive(smuggled, L"Content-Type"));
        Assert::IsTrue(EqualsAsciiCaseInsensitive(smuggled, std::wstring_view(L"content-type\0JUNK", 17)));
    }

    TEST_METHOD(TrimSpacesAndTabsTrimsBothEnds)
    {
        Assert::IsTrue(TrimSpacesAndTabs(L"  Content-Type\t ") == L"Content-Type");
        Assert::IsTrue(TrimSpacesAndTabs(L"Content-Type") == L"Content-Type");
        Assert::IsTrue(TrimSpacesAndTabs(L"\tX") == L"X");
        Assert::IsTrue(TrimSpacesAndTabs(L"X ") == L"X");
        Assert::IsTrue(TrimSpacesAndTabs(L"a b") == L"a b");  // only the ends
        Assert::IsTrue(TrimSpacesAndTabs(L"   ").empty());
        Assert::IsTrue(TrimSpacesAndTabs(L"").empty());
    }

    // The header-name match both replace: "Content-Type : x" is a header WinHttpAddRequestHeaders accepts,
    // and it must be recognised or a second Content-Type gets appended next to the operator's own.
    TEST_METHOD(TrimmedNameMatchesAHeaderWrittenLoosely)
    {
        const auto nameOf = [](std::wstring_view header) {
            return TrimSpacesAndTabs(header.substr(0, header.find(L':')));
        };

        for (const wchar_t* header : {L"Content-Type: application/json",
                                      L"Content-Type : application/json",
                                      L"content-type\t:application/json",
                                      L" Content-Type: application/json"})
        {
            Assert::IsTrue(EqualsAsciiCaseInsensitive(nameOf(header), L"Content-Type"), header);
        }

        Assert::IsFalse(EqualsAsciiCaseInsensitive(nameOf(L"X-Content-Type: a"), L"Content-Type"));
        Assert::IsFalse(EqualsAsciiCaseInsensitive(nameOf(L"X-Header: Content-Type:"), L"Content-Type"));
    }
};

}  // namespace Orc::Test
