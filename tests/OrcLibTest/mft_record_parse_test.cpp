//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "MFTRecord.h"
#include "AttributeList.h"
#include "NtfsDataStructures.h"
#include "VolumeReaderTest.h"

#include <algorithm>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// ParseRecord() walking forged file record segments. The case that matters is an attribute chain filling the segment
// exactly: there is then no room for an $END marker, and the bytes left at the last attribute are fewer than
// sizeof(ATTRIBUTE_RECORD_HEADER), which the nonresident Form union inflates to 72. A walk gated on the whole struct
// stops one attribute short and drops it silently. On a large directory that attribute is the resident $BITMAP of
// $I30, and without it GetIndexAttributes() fails and the directory's entire index is lost.
TEST_CLASS(MFTRecordParseTest)
{
    static constexpr ULONG kBytesPerFRS = 1024;
    static constexpr ULONG kBytesPerSector = 512;
    static constexpr ULONG kSectorsPerFRS = kBytesPerFRS / kBytesPerSector;
    static constexpr USHORT kFirstAttributeOffset = 56;
    static constexpr USHORT kUpdateSequenceOffset = 48;
    static constexpr USHORT kUpdateSequenceNumber = 0x0101;

    // Where an attribute record's name, then its value, start for each form.
    static constexpr size_t kResidentNameOffset = 0x18;  // FIELD_OFFSET(.., Form) + the resident part
    static constexpr size_t kNonresidentNameOffset = 0x48;  // FIELD_OFFSET(.., Form) + the nonresident part

    // A reader carrying nothing but the geometry ParseRecord() and MultiSectorFixup() read off it.
    class GeometryReader : public VolumeReaderTest
    {
    public:
        GeometryReader()
            : VolumeReaderTest(nullptr, nullptr)
        {
            m_BytesPerFRS = kBytesPerFRS;
            m_BytesPerSector = kBytesPerSector;
            m_BytesPerCluster = 4096;
            m_fsType = FSVBR::FSType::NTFS;
        }
    };

    static std::vector<BYTE>
    MakeResident(ATTRIBUTE_TYPE_CODE type, const std::wstring& name, DWORD recordLength, DWORD valueLength)
    {
        const size_t nameBytes = name.size() * sizeof(WCHAR);
        const size_t valueOffset = kResidentNameOffset + nameBytes;
        Assert::IsTrue(valueOffset + valueLength <= recordLength, L"forged resident attribute does not fit");

        std::vector<BYTE> bytes(recordLength, BYTE(0));
        ATTRIBUTE_RECORD_HEADER* header = reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(bytes.data());
        header->TypeCode = type;
        header->RecordLength = recordLength;
        header->FormCode = RESIDENT_FORM;
        header->NameLength = static_cast<UCHAR>(name.size());
        header->NameOffset = static_cast<USHORT>(name.empty() ? 0 : kResidentNameOffset);
        header->Form.Resident.ValueOffset = static_cast<USHORT>(valueOffset);
        header->Form.Resident.ValueLength = valueLength;
        if (!name.empty())
        {
            memcpy(bytes.data() + kResidentNameOffset, name.c_str(), nameBytes);
        }

        return bytes;
    }

    // A nonresident attribute record covering a single cluster, its mapping pairs placed right after the name.
    static std::vector<BYTE> MakeNonresident(ATTRIBUTE_TYPE_CODE type, const std::wstring& name, DWORD recordLength)
    {
        const size_t nameBytes = name.size() * sizeof(WCHAR);
        const size_t mappingPairsOffset = kNonresidentNameOffset + nameBytes;
        Assert::IsTrue(mappingPairsOffset + 4 <= recordLength, L"forged nonresident attribute does not fit");

        std::vector<BYTE> bytes(recordLength, BYTE(0));
        ATTRIBUTE_RECORD_HEADER* header = reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(bytes.data());
        header->TypeCode = type;
        header->RecordLength = recordLength;
        header->FormCode = NONRESIDENT_FORM;
        header->NameLength = static_cast<UCHAR>(name.size());
        header->NameOffset = static_cast<USHORT>(name.empty() ? 0 : kNonresidentNameOffset);
        header->Form.Nonresident.LowestVcn = 0;
        header->Form.Nonresident.HighestVcn = 0;
        header->Form.Nonresident.MappingPairsOffset = static_cast<USHORT>(mappingPairsOffset);
        header->Form.Nonresident.AllocatedLength = 4096;
        header->Form.Nonresident.FileSize = 4096;
        header->Form.Nonresident.ValidDataLength = 4096;
        if (!name.empty())
        {
            memcpy(bytes.data() + kNonresidentNameOffset, name.c_str(), nameBytes);
        }

        bytes[mappingPairsOffset] = 0x11;  // one run: a one byte length, a one byte offset
        bytes[mappingPairsOffset + 1] = 0x01;
        bytes[mappingPairsOffset + 2] = 0x02;

        return bytes;
    }


    // A resident $REPARSE_POINT whose value opens on the reparse tag, which is all GetReparsePointType() reads of
    // it. The DataLength is the bytes that follow the fixed fields, as it is on disk.
    static std::vector<BYTE> MakeReparsePoint(DWORD tag, DWORD recordLength, DWORD valueLength)
    {
        Assert::IsTrue(valueLength >= sizeof(REPARSE_POINT_ATTRIBUTE), L"forged reparse point value is too short");

        std::vector<BYTE> bytes = MakeResident($REPARSE_POINT, L"", recordLength, valueLength);
        REPARSE_POINT_ATTRIBUTE* reparse =
            reinterpret_cast<REPARSE_POINT_ATTRIBUTE*>(bytes.data() + kResidentNameOffset);
        reparse->TypeAndFlags = tag;
        reparse->DataLength = static_cast<USHORT>(valueLength - FIELD_OFFSET(REPARSE_POINT_ATTRIBUTE, Data));

        return bytes;
    }
    // Each sector's last two bytes hold the update sequence number on disk and are restored from the array by
    // MultiSectorFixup(). Must be re-stamped after any write that lands on a sector tail, or the fixup refuses the
    // record. The array entries stay zero: that is what those tail bytes really hold here.
    static void StampUpdateSequence(std::vector<BYTE>& record)
    {
        USHORT* updateSequenceArray = reinterpret_cast<USHORT*>(record.data() + kUpdateSequenceOffset);
        updateSequenceArray[0] = kUpdateSequenceNumber;
        for (ULONG sector = 0; sector < kSectorsPerFRS; ++sector)
        {
            updateSequenceArray[1 + sector] = 0;
            *reinterpret_cast<USHORT*>(record.data() + (sector + 1) * kBytesPerSector - 2) = kUpdateSequenceNumber;
        }
    }

    // A file record segment holding 'attributes' back to back from kFirstAttributeOffset.
    static std::vector<BYTE> MakeRecord(const std::vector<std::vector<BYTE>>& attributes, bool directory)
    {
        std::vector<BYTE> record(kBytesPerFRS, BYTE(0));
        memcpy(record.data(), "FILE", 4);

        FILE_RECORD_SEGMENT_HEADER* header = reinterpret_cast<FILE_RECORD_SEGMENT_HEADER*>(record.data());
        header->MultiSectorHeader.UpdateSequenceArrayOffset = kUpdateSequenceOffset;
        header->MultiSectorHeader.UpdateSequenceArraySize = static_cast<USHORT>(1 + kSectorsPerFRS);
        header->SequenceNumber = 1;
        header->FirstAttributeOffset = kFirstAttributeOffset;
        header->Flags =
            static_cast<USHORT>(FILE_RECORD_SEGMENT_IN_USE | (directory ? FILE_FILE_NAME_INDEX_PRESENT : 0));

        size_t offset = kFirstAttributeOffset;
        for (const std::vector<BYTE>& attribute : attributes)
        {
            Assert::IsTrue(offset + attribute.size() <= record.size(), L"forged attributes overflow the segment");
            memcpy(record.data() + offset, attribute.data(), attribute.size());
            offset += attribute.size();
        }

        StampUpdateSequence(record);
        return record;
    }

    // Overwrites the attribute the walk lands on at 'offset' with a header declaring 'formCode' over 'recordLength'
    // bytes, which is how a forged record understates the span of its own form.
    static void
    ForgeAttributeAt(std::vector<BYTE>& record, size_t offset, ATTRIBUTE_TYPE_CODE type, UCHAR formCode, DWORD recordLength)
    {
        ATTRIBUTE_RECORD_HEADER* forged = reinterpret_cast<ATTRIBUTE_RECORD_HEADER*>(record.data() + offset);
        forged->TypeCode = type;
        forged->RecordLength = recordLength;
        forged->FormCode = formCode;
        forged->NameLength = 0;
        forged->NameOffset = 0;
        StampUpdateSequence(record);
    }

    static HRESULT Parse(MFTRecord& mftRecord, std::vector<BYTE>& record, MFTRecord* baseRecord = nullptr)
    {
        const std::shared_ptr<VolumeReader> reader = std::make_shared<GeometryReader>();
        return mftRecord.ParseRecord(
            reader, reinterpret_cast<PFILE_RECORD_SEGMENT_HEADER>(record.data()), kBytesPerFRS, baseRecord);
    }

    // MFTWalker only ever hands a child record a base that has already parsed its own $ATTRIBUTE_LIST, which is
    // what allocates the list ParseRecord() registers a child's attributes into. Given directly here: what the
    // tests below are about is the registration, not the forging of a whole attribute list.
    static void GiveBaseRecordAnAttributeList(MFTRecord& baseRecord)
    {
        Assert::IsTrue(baseRecord.IsParsed(), L"the base record must be parsed before a child refers to it");
        baseRecord.m_pAttributeList = std::make_shared<AttributeList>();
    }

public:
    // The regression. 56 + 832 + 88 = 976, so the $BITMAP starts 48 bytes before the end of the segment and ends at
    // 1016, leaving 8 bytes with no room for an $END marker. 48 is less than sizeof(ATTRIBUTE_RECORD_HEADER).
    TEST_METHOD(TrailingAttributeOfAnExactlyFilledRecordIsParsed)
    {
        const std::vector<std::vector<BYTE>> attributes {
            MakeResident($INDEX_ROOT, L"$I30", 832, 700),
            MakeNonresident($INDEX_ALLOCATION, L"$I30", 88),
            MakeResident($BITMAP, L"$I30", 40, 8)};

        std::vector<BYTE> record = MakeRecord(attributes, true);
        MFTRecord mftRecord;

        Assert::IsTrue(SUCCEEDED(Parse(mftRecord, record)));
        Assert::IsTrue(mftRecord.IsParsed());
        Assert::IsNotNull(mftRecord.GetIndexRootAttribute(L"$I30").get());
        Assert::IsNotNull(mftRecord.GetIndexAllocationAttribute(L"$I30").get());
        Assert::IsNotNull(
            mftRecord.GetBitmapAttribute(L"$I30").get(), L"the trailing resident $BITMAP was dropped by the walk");
    }

    // Control: the same chain with room left for an $END marker, which has always been parsed.
    TEST_METHOD(TrailingAttributeFollowedByAnEndMarkerIsParsed)
    {
        const std::vector<std::vector<BYTE>> attributes {
            MakeResident($INDEX_ROOT, L"$I30", 800, 700),
            MakeNonresident($INDEX_ALLOCATION, L"$I30", 88),
            MakeResident($BITMAP, L"$I30", 40, 8)};

        std::vector<BYTE> record = MakeRecord(attributes, true);
        *reinterpret_cast<ATTRIBUTE_TYPE_CODE*>(record.data() + 56 + 800 + 88 + 40) = $END;
        MFTRecord mftRecord;

        Assert::IsTrue(SUCCEEDED(Parse(mftRecord, record)));
        Assert::IsNotNull(mftRecord.GetBitmapAttribute(L"$I30").get());
    }

    // Fewer bytes than a fixed attribute header cannot describe an attribute at all: the walk stops there and keeps
    // everything it has already parsed.
    TEST_METHOD(TrailingBytesTooShortForAHeaderStopTheWalk)
    {
        const std::vector<std::vector<BYTE>> attributes {MakeResident($DATA, L"", 960, 900)};

        std::vector<BYTE> record = MakeRecord(attributes, false);
        memset(record.data() + 56 + 960, 0xB0, kBytesPerFRS - (56 + 960));  // 8 bytes, short of a header
        StampUpdateSequence(record);
        MFTRecord mftRecord;

        Assert::IsTrue(SUCCEEDED(Parse(mftRecord, record)));
        Assert::IsTrue(mftRecord.IsParsed());
        Assert::AreEqual(size_t(1), mftRecord.GetDataAttributes().size());
    }

    // A RecordLength that does not cover the form the attribute declares is refused before any Form field is read:
    // the walk only guarantees the fixed header at the cursor, so reading Form would leave the record.
    TEST_METHOD(AttributeShorterThanItsDeclaredFormIsRefused)
    {
        // 56 + 952 = 1008, so the walk lands there with exactly 16 bytes left: enough for the fixed header, and
        // short of both the resident form (24 bytes) and the nonresident one (72).
        for (const UCHAR formCode : {RESIDENT_FORM, NONRESIDENT_FORM})
        {
            const std::vector<std::vector<BYTE>> attributes {MakeResident($DATA, L"", 952, 900)};
            std::vector<BYTE> record = MakeRecord(attributes, false);
            ForgeAttributeAt(record, 1008, $BITMAP, formCode, 16);
            MFTRecord mftRecord;

            Assert::IsTrue(SUCCEEDED(Parse(mftRecord, record)));
            Assert::AreEqual(size_t(1), mftRecord.GetDataAttributes().size());
            Assert::IsNull(mftRecord.GetBitmapAttribute(L"").get(), std::to_wstring(formCode).c_str());
        }
    }

    // A $REPARSE_POINT does not have to sit in the base record. A file with enough names, or a long enough
    // attribute list, keeps it in a child record, while every consumer asks the base record what kind of file this
    // is: ParseRecord() registers a child's attributes with its base, so the attribute is reachable from there and
    // only the flags were missing. The wof flag was fixed when a collected sample came back full of zeros; these
    // answer the record dump, which then named no reparse point while holding its attribute, and had no fixture
    // until ParseRecord() could be driven over a forged base and child pair.
    //
    // 'wof' is not among the tags: WOFReparseAttribute parses the reparse data at construction, so that one is
    // pinned by the xpress16k fixture with its reparse point in a child record, against the filesystem's hashes.
    TEST_METHOD(ReparsePointOfAChildRecordMarksTheBaseRecord)
    {
        const std::vector<std::pair<DWORD, bool>> tags {
            {IO_REPARSE_TAG_SYMLINK, true}, {IO_REPARSE_TAG_MOUNT_POINT, false}};

        for (const auto& [tag, isSymLink] : tags)
        {
            std::vector<BYTE> baseSegment = MakeRecord({MakeResident($DATA, L"", 952, 900)}, false);
            std::vector<BYTE> childSegment = MakeRecord({MakeReparsePoint(tag, 48, 16)}, false);
            MFTRecord baseRecord;
            MFTRecord childRecord;

            Assert::IsTrue(SUCCEEDED(Parse(baseRecord, baseSegment)), L"the base record itself");
            GiveBaseRecordAnAttributeList(baseRecord);

            Assert::IsTrue(SUCCEEDED(Parse(childRecord, childSegment, &baseRecord)), L"the child record");
            Assert::IsTrue(childRecord.HasReparsePoint(), L"the child record holding the attribute was not marked");
            Assert::IsTrue(
                baseRecord.HasReparsePoint(), L"the base record of a reparse point held in a child was not marked");
            Assert::AreEqual(isSymLink, baseRecord.IsSymbolicLink(), L"base record symlink flag");
            Assert::AreEqual(!isSymLink, baseRecord.IsJunction(), L"base record junction flag");
            Assert::IsFalse(baseRecord.IsOverlayFile(), L"neither tag is a wof overlay");

            // The other half of the blind spot: the attribute always did reach the base record, so the dump read
            // the flags as "no reparse point" over an attribute list that held one.
            const std::vector<AttributeListEntry>& entries = baseRecord.GetAttributeList();
            Assert::IsTrue(
                std::any_of(
                    entries.cbegin(),
                    entries.cend(),
                    [](const AttributeListEntry& entry) { return entry.TypeCode() == $REPARSE_POINT; }),
                L"the child's attribute was not registered with the base record");
        }
    }

    // A tag this code knows nothing about is still a reparse point, and the kind flags stay off: the dump prints
    // [reparse point] alone for it, on the base record as on the child.
    TEST_METHOD(UnknownReparseTagMarksNothingButTheReparsePoint)
    {
        std::vector<BYTE> baseSegment = MakeRecord({MakeResident($DATA, L"", 952, 900)}, false);
        std::vector<BYTE> childSegment = MakeRecord({MakeReparsePoint(IO_REPARSE_TAG_DEDUP, 48, 16)}, false);
        MFTRecord baseRecord;
        MFTRecord childRecord;

        Assert::IsTrue(SUCCEEDED(Parse(baseRecord, baseSegment)));
        GiveBaseRecordAnAttributeList(baseRecord);

        Assert::IsTrue(SUCCEEDED(Parse(childRecord, childSegment, &baseRecord)));
        Assert::IsTrue(baseRecord.HasReparsePoint());
        Assert::IsFalse(baseRecord.IsSymbolicLink());
        Assert::IsFalse(baseRecord.IsJunction());
        Assert::IsFalse(baseRecord.IsOverlayFile());
    }

    // The record holding its own reparse point keeps answering for itself, which is every file small enough to fit
    // in one record.
    TEST_METHOD(ReparsePointOfABaseRecordMarksThatRecord)
    {
        std::vector<BYTE> record = MakeRecord({MakeReparsePoint(IO_REPARSE_TAG_MOUNT_POINT, 48, 16)}, false);
        MFTRecord mftRecord;

        Assert::IsTrue(SUCCEEDED(Parse(mftRecord, record)));
        Assert::IsTrue(mftRecord.HasReparsePoint());
        Assert::IsTrue(mftRecord.IsJunction());
        Assert::IsFalse(mftRecord.IsSymbolicLink());
    }
};

}  // namespace Orc::Test
