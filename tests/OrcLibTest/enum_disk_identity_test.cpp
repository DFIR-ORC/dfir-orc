//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "EnumDisk.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// The two identity parsers of EnumDisk, fed the forged bytes a device controls: the offsets and strings of a
// STORAGE_DEVICE_DESCRIPTOR, and the word pairs of an ATA IDENTIFY DEVICE page. The cases that must not read past
// their answer place it flush against a PAGE_NOACCESS page, so a lost bound faults instead of passing quietly.
TEST_CLASS(EnumDiskIdentityTest)
{
    class GuardedBuffer
    {
    public:
        GuardedBuffer()
        {
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            m_pageSize = info.dwPageSize;
            m_base =
                static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * m_pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Assert::IsNotNull(m_base);
            DWORD old = 0;
            Assert::IsTrue(VirtualProtect(m_base + m_pageSize, m_pageSize, PAGE_NOACCESS, &old) != FALSE);
        }

        ~GuardedBuffer() { VirtualFree(m_base, 0, MEM_RELEASE); }

        // The bytes end on the last readable byte of the first page, the next one faults
        BasicBufferSpan<const uint8_t> Place(const std::vector<uint8_t>& bytes)
        {
            Assert::IsTrue(bytes.size() <= m_pageSize);
            uint8_t* start = m_base + m_pageSize - bytes.size();
            std::copy(bytes.begin(), bytes.end(), start);
            return BasicBufferSpan<const uint8_t>(start, bytes.size());
        }

    private:
        uint8_t* m_base = nullptr;
        size_t m_pageSize = 0;
    };

    static BasicBufferSpan<const uint8_t> Span(const std::vector<uint8_t>& buffer)
    {
        return BasicBufferSpan<const uint8_t>(buffer.data(), buffer.size());
    }

    // A descriptor of 'size' bytes filled with 'filler', carrying 'text' at 'offset'. The storage stack NUL
    // terminates every string its descriptor points to, so 'terminate' false forges one that does not.
    static std::vector<uint8_t> Descriptor(
        size_t size, size_t offset, std::string_view text, bool terminate = true, uint8_t filler = 0x00)
    {
        std::vector<uint8_t> buffer(size, filler);
        for (size_t i = 0; i < text.size(); ++i)
        {
            buffer[offset + i] = static_cast<uint8_t>(text[i]);
        }

        if (terminate)
        {
            buffer[offset + text.size()] = 0x00;
        }

        return buffer;
    }

    // An IDENTIFY DEVICE page of 'size' bytes carrying the raw bytes 'text' at word 'firstWord'
    static std::vector<uint8_t> IdentifyPage(size_t size, size_t firstWord, std::string_view text)
    {
        std::vector<uint8_t> buffer(size, 0x00);
        for (size_t i = 0; i < text.size(); ++i)
        {
            buffer[firstWord * 2 + i] = static_cast<uint8_t>(text[i]);
        }

        return buffer;
    }

    //
    // STORAGE_DEVICE_DESCRIPTOR strings
    //

    TEST_METHOD(DescriptorStringTrimsTheInquiryPadding)
    {
        // INQUIRY pads its fixed width fields with spaces
        const auto descriptor = Descriptor(64, 0x20, "Virtual Disk    ");
        Assert::AreEqual(std::wstring(L"Virtual Disk"), EnumDisk::ParseDescriptorString(Span(descriptor), 0x20));
    }

    TEST_METHOD(DescriptorStringIsEmptyWhenTheOffsetIsZero)
    {
        // 0 is how the descriptor says a field is absent, whatever the byte at 0 happens to be
        const auto descriptor = Descriptor(64, 0, "NOT A FIELD");
        Assert::AreEqual(std::wstring(), EnumDisk::ParseDescriptorString(Span(descriptor), 0));
    }

    TEST_METHOD(DescriptorStringIsEmptyWhenTheOffsetIsPastTheBuffer)
    {
        const auto descriptor = Descriptor(64, 0x20, "Virtual Disk");
        Assert::AreEqual(std::wstring(), EnumDisk::ParseDescriptorString(Span(descriptor), 64 + 8));
    }

    TEST_METHOD(DescriptorStringIsEmptyWithoutATerminatorInsideTheBuffer)
    {
        // Printable from the offset to the last byte and never terminated: the only thing wrong is the terminator,
        // and the scan has to stop at the end of the answer on its own
        GuardedBuffer guarded;
        const auto descriptor = guarded.Place(Descriptor(64, 0x20, std::string(32, 'X'), false, 'X'));
        Assert::AreEqual(std::wstring(), EnumDisk::ParseDescriptorString(descriptor, 0x20));
    }

    TEST_METHOD(DescriptorStringStopsAtANonPrintableByte)
    {
        const auto descriptor = Descriptor(
            64,
            0x20,
            std::string_view(
                "AB\x01"
                "CD",
                5));
        Assert::AreEqual(std::wstring(L"AB"), EnumDisk::ParseDescriptorString(Span(descriptor), 0x20));
    }

    TEST_METHOD(DescriptorStringIsEmptyWhenTheDeviceReportsOnlyPadding)
    {
        const auto descriptor = Descriptor(64, 0x20, "        ");
        Assert::AreEqual(std::wstring(), EnumDisk::ParseDescriptorString(Span(descriptor), 0x20));
    }

    //
    // ATA IDENTIFY DEVICE strings
    //

    TEST_METHOD(AtaIdentifyStringSwapsEveryWord)
    {
        // ATA holds the earlier character in the high byte of each word, so the raw bytes of "Samsung " read
        // "aSsmnu g". The model number is 20 words from word 27.
        const auto page = IdentifyPage(512, 27, "aSsmnu g");
        Assert::AreEqual(std::wstring(L"Samsung"), EnumDisk::ParseAtaIdentifyString(Span(page), 27, 4));
    }

    TEST_METHOD(AtaIdentifyStringTrimsTheTrailingPadding)
    {
        // ATA strings are space padded to their fixed width and are not NUL terminated
        const auto page = IdentifyPage(512, 10, "aS    ");
        Assert::AreEqual(std::wstring(L"Sa"), EnumDisk::ParseAtaIdentifyString(Span(page), 10, 3));
    }

    TEST_METHOD(AtaIdentifyStringIsEmptyWhenTheWordRangeIsPastThePage)
    {
        // A device that answered 40 bytes holds no word 27: the range has to be refused before it is read
        GuardedBuffer guarded;
        const auto page = guarded.Place(IdentifyPage(40, 0, "aSsmnu g"));
        Assert::AreEqual(std::wstring(), EnumDisk::ParseAtaIdentifyString(page, 27, 20));
    }

    TEST_METHOD(AtaIdentifyStringStopsAtANonPrintableByte)
    {
        // Swapped, these four bytes read 'A' 'B' 0x01 'D'
        const auto page = IdentifyPage(512, 10, std::string_view("BAD\x01", 4));
        Assert::AreEqual(std::wstring(L"AB"), EnumDisk::ParseAtaIdentifyString(Span(page), 10, 2));
    }
};

}  // namespace Orc::Test
