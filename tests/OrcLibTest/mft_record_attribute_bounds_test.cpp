//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "MftRecordAttribute.h"
#include "NtfsDataStructures.h"
#include "Utils/BufferSpan.h"

#include <algorithm>
#include <array>
#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// Resident attributes built from forged bytes. Each attribute record ends right before a PAGE_NOACCESS page, so a
// read past the record faults; a read past the value but inside the record is caught by the assertions instead.
TEST_CLASS(MftRecordAttributeBoundsTest)
{
    static const size_t kValueOffset = 0x18;  // FIELD_OFFSET(ATTRIBUTE_RECORD_HEADER, Form) + resident part

    class GuardedBuffer
    {
    public:
        GuardedBuffer()
        {
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            m_pageSize = info.dwPageSize;
            m_base = static_cast<BYTE*>(VirtualAlloc(nullptr, 2 * m_pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Assert::IsNotNull(m_base);
            DWORD old = 0;
            Assert::IsTrue(VirtualProtect(m_base + m_pageSize, m_pageSize, PAGE_NOACCESS, &old) != FALSE);
        }

        ~GuardedBuffer() { VirtualFree(m_base, 0, MEM_RELEASE); }

        PATTRIBUTE_RECORD_HEADER Place(const std::vector<BYTE>& bytes)
        {
            Assert::IsTrue(bytes.size() <= m_pageSize);
            BYTE* start = m_base + m_pageSize - bytes.size();
            std::copy(bytes.begin(), bytes.end(), start);
            return reinterpret_cast<PATTRIBUTE_RECORD_HEADER>(start);
        }

    private:
        BYTE* m_base = nullptr;
        size_t m_pageSize = 0;
    };

    // A resident attribute of 'recordLength' bytes whose value is 'value', padded with 'fill' up to recordLength.
    static std::vector<BYTE>
    MakeResident(ATTRIBUTE_TYPE_CODE type, const std::vector<BYTE>& value, size_t recordLength, BYTE fill = 0)
    {
        Assert::IsTrue(kValueOffset + value.size() <= recordLength);
        std::vector<BYTE> bytes(recordLength, fill);
        ATTRIBUTE_RECORD_HEADER* header = reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(bytes.data());
        std::fill(bytes.begin(), bytes.begin() + kValueOffset, BYTE(0));
        header->TypeCode = type;
        header->RecordLength = static_cast<ULONG>(recordLength);
        header->FormCode = RESIDENT_FORM;
        header->Form.Resident.ValueOffset = static_cast<USHORT>(kValueOffset);
        header->Form.Resident.ValueLength = static_cast<ULONG>(value.size());
        std::copy(value.begin(), value.end(), bytes.begin() + kValueOffset);
        return bytes;
    }

    static std::vector<BYTE> MakeReparseValue(DWORD tag, const std::vector<BYTE>& data, USHORT declaredLength)
    {
        std::vector<BYTE> value(FIELD_OFFSET(REPARSE_POINT_ATTRIBUTE, Data) + data.size(), 0);
        REPARSE_POINT_ATTRIBUTE* reparse = reinterpret_cast<REPARSE_POINT_ATTRIBUTE*>(value.data());
        reparse->TypeAndFlags = tag;
        reparse->DataLength = declaredLength;
        std::copy(data.begin(), data.end(), value.begin() + FIELD_OFFSET(REPARSE_POINT_ATTRIBUTE, Data));
        return value;
    }

    // A mount point reparse buffer with one name of 'nameBytes' bytes used as both substitute and print name.
    static std::vector<BYTE> MakeMountPointData(size_t nameBytes)
    {
        std::vector<BYTE> data(FIELD_OFFSET(REPARSE_POINT_DATA, Data) + nameBytes, 'a');
        REPARSE_POINT_DATA* point = reinterpret_cast<REPARSE_POINT_DATA*>(data.data());
        point->SubstituteNameOffset = 0;
        point->SubstituteNameLength = static_cast<USHORT>(nameBytes);
        point->PrintNameOffset = 0;
        point->PrintNameLength = static_cast<USHORT>(nameBytes);
        point->Padding = 0;
        return data;
    }

    static std::vector<BYTE> MakeWofData()
    {
        const std::array<DWORD, 4> layout {1, 2, 1, 0};  // version, provider (file), provider version, XPRESS4K

        std::vector<BYTE> data(layout.size() * sizeof(DWORD), 0);
        const BufferSpan bytes(data.data(), data.size());
        std::copy_n(reinterpret_cast<const uint8_t*>(layout.data()), bytes.size(), bytes.begin());
        return data;
    }

public:
    TEST_METHOD(GetResidentValueIsBoundedByTheAttribute)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> value(16, 0x42);

        PATTRIBUTE_RECORD_HEADER header = guard.Place(MakeResident($DATA, value, 0x30));
        const BufferView view = GetResidentValue(header);
        Assert::AreEqual(size_t(16), view.size());
        Assert::IsTrue(view.data() == reinterpret_cast<const uint8_t*>(header) + kValueOffset);

        Assert::AreEqual(size_t(0), GetResidentValue(nullptr).size());

        std::vector<BYTE> bytes = MakeResident($DATA, value, 0x30);
        ATTRIBUTE_RECORD_HEADER* forged = reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(bytes.data());

        forged->Form.Resident.ValueLength = 0x30 - static_cast<ULONG>(kValueOffset) + 1;
        Assert::AreEqual(size_t(0), GetResidentValue(guard.Place(bytes)).size());

        forged->Form.Resident.ValueLength = 0xFFFFFFFF;
        Assert::AreEqual(size_t(0), GetResidentValue(guard.Place(bytes)).size());

        forged->Form.Resident.ValueLength = 0;
        forged->Form.Resident.ValueOffset = 0x31;
        Assert::AreEqual(size_t(0), GetResidentValue(guard.Place(bytes)).size());

        forged->Form.Resident.ValueOffset = static_cast<USHORT>(kValueOffset);
        forged->FormCode = NONRESIDENT_FORM;
        Assert::AreEqual(size_t(0), GetResidentValue(guard.Place(bytes)).size());
    }

    // The reparse tag is read only when the value holds it.
    TEST_METHOD(ReparsePointTypeNeedsAWholeTag)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> tag = {0x03, 0x00, 0x00, 0xA0};  // IO_REPARSE_TAG_MOUNT_POINT

        Assert::AreEqual(
            static_cast<int>(IO_REPARSE_TAG_MOUNT_POINT),
            static_cast<int>(ReparsePointAttribute::GetReparsePointType(guard.Place(MakeResident($REPARSE_POINT, tag, 0x1C)))));

        // Two value bytes, the tag's other two left in the record: not a tag.
        Assert::AreEqual(
            0,
            static_cast<int>(ReparsePointAttribute::GetReparsePointType(
                guard.Place(MakeResident($REPARSE_POINT, {0x03, 0x00}, 0x1C, 0xA0)))));
    }

    // Names are resolved inside the value: a reparse buffer that only fits by borrowing the record's bytes past
    // ValueLength is refused, while the same buffer with a correct ValueLength parses.
    TEST_METHOD(ReparsePointParseStaysInsideTheValue)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> data = MakeMountPointData(8);
        const std::vector<BYTE> value = MakeReparseValue(IO_REPARSE_TAG_MOUNT_POINT, data, static_cast<USHORT>(data.size()));

        ReparsePointAttribute whole(guard.Place(MakeResident($REPARSE_POINT, value, kValueOffset + value.size())), nullptr);
        Assert::IsTrue(SUCCEEDED(whole.Parse()));
        Assert::AreEqual(size_t(4), whole.SubstituteName().size());

        std::vector<BYTE> shortValue = MakeResident($REPARSE_POINT, value, kValueOffset + value.size());
        reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(shortValue.data())->Form.Resident.ValueLength -= 4;
        ReparsePointAttribute borrowed(guard.Place(shortValue), nullptr);
        Assert::IsTrue(FAILED(borrowed.Parse()));

        std::vector<BYTE> headerOnly = MakeResident($REPARSE_POINT, value, kValueOffset + value.size());
        reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(headerOnly.data())->Form.Resident.ValueLength = 4;
        ReparsePointAttribute truncated(guard.Place(headerOnly), nullptr);
        Assert::IsTrue(FAILED(truncated.Parse()));
    }

    // The WOF DataLength comes from disk: past the value it is refused before the WOF header is read.
    TEST_METHOD(WofReparseRefusesADataLengthPastTheValue)
    {
        GuardedBuffer guard;
        const DWORD kWofTag = 0x80000017;
        const std::vector<BYTE> data = MakeWofData();

        {
            const std::vector<BYTE> value = MakeReparseValue(kWofTag, data, static_cast<USHORT>(data.size()));
            std::error_code ec;
            WOFReparseAttribute wof(guard.Place(MakeResident($REPARSE_POINT, value, kValueOffset + value.size())), nullptr, ec);
            Assert::IsFalse(static_cast<bool>(ec));
        }

        for (const USHORT declared : {USHORT(data.size() + 1), USHORT(0x1000), USHORT(0xFFFF)})
        {
            const std::vector<BYTE> value = MakeReparseValue(kWofTag, data, declared);
            std::error_code ec;
            WOFReparseAttribute wof(guard.Place(MakeResident($REPARSE_POINT, value, kValueOffset + value.size())), nullptr, ec);
            Assert::IsTrue(static_cast<bool>(ec), std::to_wstring(declared).c_str());
        }

        const std::vector<BYTE> headerOnly = {0x17, 0x00, 0x00, 0x80, 0x10};
        std::error_code ec;
        WOFReparseAttribute truncated(guard.Place(MakeResident($REPARSE_POINT, headerOnly, kValueOffset + headerOnly.size())), nullptr, ec);
        Assert::IsTrue(static_cast<bool>(ec));
    }

    // An $INDEX_ROOT is mapped only over a value holding a whole INDEX_ROOT; Open() finds the same value.
    TEST_METHOD(IndexRootNeedsAWholeIndexRoot)
    {
        GuardedBuffer guard;
        std::vector<BYTE> root(sizeof(INDEX_ROOT), 0);
        reinterpret_cast<INDEX_ROOT*>(root.data())->BytesPerIndexBuffer = 0x1000;
        reinterpret_cast<INDEX_ROOT*>(root.data())->IndexHeader.FirstIndexEntry = sizeof(INDEX_HEADER);

        IndexRootAttribute whole(guard.Place(MakeResident($INDEX_ROOT, root, kValueOffset + root.size())), nullptr);
        Assert::IsTrue(SUCCEEDED(whole.Open()));
        Assert::AreEqual(0x1000, static_cast<int>(whole.SizePerIndex()));

        std::vector<BYTE> shortRoot(root.begin(), root.end() - 1);
        IndexRootAttribute truncated(guard.Place(MakeResident($INDEX_ROOT, shortRoot, kValueOffset + shortRoot.size())), nullptr);
        Assert::AreEqual(0, static_cast<int>(truncated.SizePerIndex()));
        Assert::IsNull(truncated.FirstIndexEntry());
        Assert::IsTrue(FAILED(truncated.Open()));
    }
};

}  // namespace Orc::Test
