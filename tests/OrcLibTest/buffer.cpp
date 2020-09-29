//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CppUnitTest.h"

#include "Buffer.h"

#include <filesystem>
#include <variant>

#include <fmt/format.h>
//#include <fmt/time.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

using namespace std;
using namespace std::string_literals;

using namespace Orc;

namespace Orc::Test {
TEST_CLASS(BufferTest) {public : BufferTest() {}

                        TEST_METHOD(Construct) {Buffer<BYTE, 4> buffer4 {0xFF, 0xFE, 0xFD, 0xFC};
Assert::IsTrue(buffer4.is_inner());

Buffer<BYTE, 4> buffer4_heap {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
Assert::IsTrue(buffer4_heap.is_heap());

Buffer<BYTE, 8> buffer8 {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
Assert::IsTrue(buffer8.is_inner());

Buffer<BYTE, 8>
    buffer8_heap {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
Assert::IsTrue(buffer8_heap.is_heap());

Buffer<BYTE, 8> empty;
Assert::IsTrue(empty.empty());
}  // namespace Orc::Test

TEST_METHOD(Capacity)
{
    Buffer<BYTE, 16> buffer;

    Assert::IsTrue(buffer.empty());

    Assert::AreEqual(0LU, buffer.capacity());
    Assert::AreEqual(0LU, buffer.size());

    buffer.reserve(50);
    Assert::AreEqual(50LU, buffer.capacity());
    Assert::AreEqual(00LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());

    buffer.resize(30);
    Assert::AreEqual(50LU, buffer.capacity());
    Assert::AreEqual(30LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());

    buffer.resize(4);
    Assert::AreEqual(50LU, buffer.capacity());
    Assert::AreEqual(04LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());

    buffer.reserve(4);
    Assert::AreEqual(16LU, buffer.capacity());
    Assert::AreEqual(04LU, buffer.size());  // When store is InnerStore, capacity is fix at _DeclElts
    Assert::IsTrue(buffer.is_inner());

    buffer.reserve(0);
    Assert::AreEqual(0LU, buffer.capacity());
    Assert::AreEqual(0LU, buffer.size());  // When store is Empty, capacity is fix at 0
    Assert::IsTrue(buffer.empty());
}
TEST_METHOD(ResizeNoContent)
{
    Buffer<BYTE, 16> buffer;

    buffer.resize(4);
    Assert::AreEqual(16LU, buffer.capacity());  // When store is InnerStore, capacity is fix at _DeclElts
    Assert::AreEqual(04LU, buffer.size());
    Assert::IsTrue(buffer.is_inner());

    buffer.resize(64);
    Assert::AreEqual(64LU, buffer.capacity());
    Assert::AreEqual(64LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());

    Assert::ExpectException<Orc::Exception&>([&buffer]() { buffer.resize(128, false); });

    buffer.resize(128);
    Assert::AreEqual(128LU, buffer.capacity());
    Assert::AreEqual(128LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());

    buffer.resize(0);
    Assert::AreEqual(128LU, buffer.capacity());
    Assert::AreEqual(000LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());
}
TEST_METHOD(Assign)
{
    BYTE buf[] = {0, 1, 2, 3, 4};
    BYTE buf2[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    Buffer<BYTE, 16> buffer;

    buffer.assign(buf, 5);
    Assert::AreEqual(16LU, buffer.capacity());
    Assert::AreEqual(05LU, buffer.size());
    Assert::IsTrue(buffer.is_inner());
    for (ULONG i = 0; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buffer[i], buf[i]);
    }
    buffer.assign(buf2, 26);
    Assert::AreEqual(26LU, buffer.capacity());
    Assert::AreEqual(26LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());
    for (ULONG i = 0; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buffer[i], buf2[i]);
    }
    Buffer<BYTE, 4> buffer4 {0xFF, 0xFE, 0xFD, 0xFC};
    Assert::IsTrue(buffer4.is_inner());

    buffer.assign(buffer4);
    Assert::AreEqual(26LU, buffer.capacity());
    Assert::AreEqual(04LU, buffer.size());
    Assert::IsTrue(buffer.is_heap());
    for (ULONG i = 0; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buffer[i], buffer4[i]);
    }

    buffer.clear();
    buffer.assign(buffer4);
    Assert::AreEqual(16LU, buffer.capacity());
    Assert::AreEqual(04LU, buffer.size());
    Assert::IsTrue(buffer.is_inner());
    for (ULONG i = 0; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buffer[i], buffer4[i]);
    }
}
TEST_METHOD(ViewStore)
{
    BYTE buf[] = {0, 1, 2, 3, 4};
    BYTE buf2[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    Buffer<BYTE, 16> buffer;

    {
        const auto& cbuffer = buffer.view_of(buf, 5, 5);

        Assert::AreEqual(5LU, cbuffer.capacity());
        Assert::AreEqual(5LU, cbuffer.size());
        Assert::IsTrue(cbuffer.is_view());
        for (ULONG i = 0; i < cbuffer.size(); ++i)
        {
            Assert::AreEqual(cbuffer[i], buf[i]);
        }
    }

    {
        const auto& cbuffer = buffer.view_of(buf2, 26, 26);
        Assert::AreEqual(26LU, cbuffer.capacity());
        Assert::AreEqual(26LU, cbuffer.size());
        Assert::IsTrue(cbuffer.is_view());
        for (ULONG i = 0; i < cbuffer.size(); ++i)
        {
            Assert::AreEqual(cbuffer[i], buf2[i]);
        }
    }

    Buffer<BYTE, 16> addition;

    buffer.view_of(buf, 5, 5);
    addition.view_of(buffer, 5);

    buffer.view_of(buf2, 26, 26);
    addition += buffer;

    Assert::AreEqual(31LU, addition.capacity());
    Assert::AreEqual(31LU, addition.size());

    for (ULONG i = 0; i < 5; ++i)
    {
        Assert::AreEqual(addition[i], buf[i]);
    }
    for (ULONG i = 0; i < 26; ++i)
    {
        Assert::AreEqual(addition[5 + i], buf2[i]);
    }

    buffer.view_of(buf2, 26);
    Assert::AreEqual(26LU, buffer.capacity());
    Assert::AreEqual(0LU, buffer.size());

    for (BYTE i = 0; i < 26; ++i)
    {
        buffer += i + 8;
    }

    Assert::AreEqual(26LU, buffer.capacity());
    Assert::AreEqual(26LU, buffer.size());

    for (BYTE i = 0; i < 26; ++i)
    {
        Assert::AreEqual(buffer[i], (BYTE)(i + 8));
    }

    buffer += 56;
}
TEST_METHOD(Append)
{
    BYTE buf[] = {0, 1, 2, 3, 4};
    BYTE buf2[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    Buffer<BYTE, 16> buffer;

    auto buf_size = sizeof(buf) / sizeof(BYTE);
    buffer.append(buf, (ULONG)buf_size);
    Assert::AreEqual(16LU, buffer.capacity());
    Assert::AreEqual((ULONG)buf_size, buffer.size());
    Assert::IsTrue(buffer.is_inner());
    for (ULONG i = 0; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buf[i], buffer[i]);
    }

    buffer.append(buf, (ULONG)buf_size);
    Assert::AreEqual(16LU, buffer.capacity());
    Assert::AreEqual((ULONG)buf_size * 2, buffer.size());
    Assert::IsTrue(buffer.is_inner());
    for (ULONG i = 0; i < buf_size; ++i)
    {
        Assert::AreEqual(buf[i], buffer[i]);
    }
    for (ULONG i = (ULONG)buf_size; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buf[i - buf_size], buffer[i]);
    }

    auto buf2_size = sizeof(buf2) / sizeof(BYTE);
    buffer.append(buf2, (ULONG)buf2_size);
    Assert::AreEqual((ULONG)(buf_size * 2 + buf2_size), buffer.capacity());
    Assert::AreEqual((ULONG)(buf_size * 2 + buf2_size), buffer.size());
    Assert::IsTrue(buffer.is_heap());
    for (ULONG i = (ULONG)buf_size * 2; i < buffer.size(); ++i)
    {
        Assert::AreEqual(buf2[i - (buf_size * 2)], buffer[i]);
    }

    Buffer<BYTE, 4> buffer4 {0xFF, 0xFE, 0xFD, 0xFC};
    buffer += buffer4;
    Assert::AreEqual((ULONG)(buf_size * 2 + buf2_size + buffer4.size()), buffer.capacity());
    Assert::AreEqual((ULONG)(buf_size * 2 + buf2_size + buffer4.size()), buffer.size());
    Assert::IsTrue(buffer.is_heap());

    BYTE final_buf[] = {0,  1,  2,  3,  4,  0,  1,  2,  3,  4,  0,  1,  2,  3,  4,  5,  6,    7,    8,    9,
                        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0xFF, 0xFE, 0xFD, 0xFC};
    auto final_buf_size = sizeof(final_buf) / sizeof(BYTE);
    for (ULONG i = 0; i < final_buf_size; ++i)
    {
        Assert::AreEqual(final_buf[i], buffer[i]);
    }
}
TEST_METHOD(Iterate)
{
    Buffer<BYTE, 16> empty = {};
    Buffer<BYTE, 16> inner = {0, 1, 2, 3, 4};
    Buffer<BYTE, 16> heap = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                             13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    Assert::IsTrue(empty.empty());
    for (ULONG i = 0; i < empty.size(); ++i)
    {
        Assert::AreEqual((BYTE)i, empty[i]);
    }

    Assert::IsTrue(inner.is_inner());
    for (ULONG i = 0; i < inner.size(); ++i)
    {
        Assert::AreEqual((BYTE)i, inner[i]);
    }

    Assert::IsTrue(heap.is_heap());
    for (ULONG i = 0; i < heap.size(); ++i)
    {
        Assert::AreEqual((BYTE)i, heap[i]);
    }

    for (ULONG i = 0; i < heap.size(); ++i)
    {
        heap[i] *= 2;
    }

    for (ULONG i = 0; i < heap.size(); ++i)
    {
        BYTE expect = (BYTE)i * 2;
        Assert::AreEqual(expect, heap[i]);
    }
}

TEST_METHOD(ForLoop)
{
    Buffer<BYTE, 16> empty = {};
    Buffer<BYTE, 16> inner = {0, 1, 2, 3, 4};
    Buffer<BYTE, 16> heap = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                             13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    auto test_loop = [](Buffer<BYTE, 16>& buffer) {
        int i = 0;
        for (const auto& elt : buffer)
        {
            Assert::AreEqual((BYTE)i, elt);
            ++i;
        }

        for (auto& elt : buffer)
        {
            elt *= 2;
        }

        i = 0;
        for (const auto& elt : buffer)
        {
            BYTE expect = (BYTE)i * 2;
            Assert::AreEqual(expect, elt);
            ++i;
        }
    };

    Assert::IsTrue(empty.empty());
    test_loop(empty);

    Assert::IsTrue(inner.is_inner());
    test_loop(inner);

    Assert::IsTrue(heap.is_heap());
    test_loop(heap);
}
TEST_METHOD(Convert)
{
    Buffer<BYTE, 16> _empty = {};

    Assert::ExpectException<Orc::Exception&>([&_empty]() { auto dword = (DWORD)_empty; });

    Buffer<BYTE, 16> _byte = {0xFE};
    auto a_byte = (BYTE)_byte;
    Assert::AreEqual((BYTE)0xFE, a_byte);

    Buffer<BYTE, 16> _short = {0, 1};
    auto a_short = (SHORT)_short;
    Assert::AreEqual((SHORT)0x0100, (SHORT)a_short);

    Buffer<BYTE, 16> _dword32 = {0, 1, 2, 3};
    auto a_dword32 = (DWORD32)_dword32;
    Assert::AreEqual((DWORD32)0x03020100, (DWORD32)a_dword32);

    Buffer<BYTE, 16> _dword64 = {0, 1, 2, 3, 4, 5, 6, 7};
    auto a_dword64 = (DWORD64)_dword64;
    Assert::AreEqual((DWORD64)0x0706050403020100, (DWORD64)a_dword64);

    Buffer<BYTE, 16> heap = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                             13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    struct ManyBytes
    {
        BYTE bytes[26];
    };

    auto a_manybytes = (ManyBytes)heap;

    auto heap_size = sizeof(heap) / sizeof(BYTE);
    int i = 0;
    for (const auto& elt : heap)
    {
        Assert::AreEqual((BYTE)elt, (BYTE)a_manybytes.bytes[i]);
        ++i;
    }
}
TEST_METHOD(Format)
{
    Buffer<BYTE, 16> heap = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                             13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    auto result = fmt::format("{0:02X}", heap);

    Assert::AreEqual(result, fmt::to_string("000102030405060708090A0B0C0D0E0F10111213141516171819"));

    auto wresult = fmt::format(L"{0:02X}", heap);

    Assert::AreEqual(wresult, fmt::to_wstring(L"000102030405060708090A0B0C0D0E0F10111213141516171819"));

    Buffer<DWORD, 16> dwords = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};

    auto result_hex = dwords.AsHexString();

    Assert::AreEqual(
        result_hex,
        fmt::to_wstring(L"000000000100000002000000030000000400000005000000060000000700000008000000090000000A0000000B00"
                        L"00000C0000000D0000000E0000000F00000010000000110000001200000013000000140000001500000016000000"
                        L"170000001800000019000000"));

    auto result_hex_limit = dwords.AsHexString(true, 8);

    Assert::AreEqual(result_hex_limit, fmt::to_wstring(L"0x0000000001000000"));

    Detail::BufferView<UCHAR> bv = dwords;
    // auto result_view = fmt::format("{0:02X}", bv);

    return;
}
}
;
}
