//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "Text/CryptString.h"

#include <wincrypt.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// CryptBinaryToSingleLineString asks CryptoAPI for CRYPT_STRING_NOCRLF and falls back to BinaryToSingleLineString
// where the flag is refused (XP, 2003). This system supports the flag, so the fallback is tested directly: it must
// give exactly what the flag gives.
TEST_CLASS(CryptStringTest)
{
    static std::wstring Native(const std::vector<BYTE>& data, DWORD format)
    {
        const DWORD size = static_cast<DWORD>(data.size());
        const DWORD flags = format | CRYPT_STRING_NOCRLF;
        DWORD length = 0;
        Assert::IsTrue(CryptBinaryToStringW(data.data(), size, flags, nullptr, &length) != FALSE);
        std::wstring text(length, L'\0');
        Assert::IsTrue(CryptBinaryToStringW(data.data(), size, flags, text.data(), &length) != FALSE);
        text.resize(length);
        return text;
    }

public:
    // Sizes around the 64-character line CryptoAPI breaks base64 at, and around the 16-byte hex line.
    TEST_METHOD(MatchesTheNoCrlfFlagForEverySize)
    {
        for (const DWORD format : {DWORD(CRYPT_STRING_BASE64), DWORD(CRYPT_STRING_HEX)})
        {
            for (size_t size = 1; size <= 300; ++size)
            {
                std::vector<BYTE> data(size);
                for (size_t i = 0; i < size; ++i)
                {
                    data[i] = static_cast<BYTE>(i * 37 + size);
                }

                const std::wstring native = Native(data, format);
                const std::wstring what = L"format " + std::to_wstring(format) + L", size " + std::to_wstring(size);

                const auto fallback = BinaryToSingleLineString(BufferView(data.data(), data.size()), format);
                Assert::IsFalse(fallback.has_error(), what.c_str());
                Assert::AreEqual(native, *fallback, what.c_str());
                Assert::IsTrue(fallback->find_first_of(L"\r\n") == std::wstring::npos, what.c_str());

                const auto crypt = CryptBinaryToSingleLineString(BufferView(data.data(), data.size()), format);
                Assert::IsFalse(crypt.has_error(), what.c_str());
                Assert::AreEqual(native, *crypt, what.c_str());
            }
        }
    }

    TEST_METHOD(UnsupportedFormatIsRefused)
    {
        const std::vector<BYTE> data(4, 0x01);
        Assert::IsTrue(BinaryToSingleLineString(BufferView(data.data(), data.size()), CRYPT_STRING_HEXRAW).has_error());
    }

    TEST_METHOD(EmptyInputGivesEmptyText)
    {
        const auto text = BinaryToSingleLineString(BufferView(), CRYPT_STRING_BASE64);
        Assert::IsFalse(text.has_error());
        Assert::IsTrue(text->empty());
    }

    // A caller passing CRYPT_STRING_NOCRLF itself gets the same text: the flag is never handed to CryptoAPI.
    TEST_METHOD(ANoCrlfFlagFromTheCallerIsIgnored)
    {
        const std::vector<BYTE> data(100, 0xAB);
        const auto plain = BinaryToSingleLineString(BufferView(data.data(), data.size()), CRYPT_STRING_BASE64);
        const auto flagged =
            BinaryToSingleLineString(BufferView(data.data(), data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF);
        Assert::AreEqual(*plain, *flagged);
    }
};

}  // namespace Orc::Test
