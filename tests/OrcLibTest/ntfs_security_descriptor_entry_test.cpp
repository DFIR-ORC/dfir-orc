//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "Filesystem/Ntfs/Attribute/SecurityDescriptorEntry.h"
#include "NtfsDataStructures.h"

#include <sddl.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

// $Secure:$SDS entries as MFTWalker hands them to NTFSInfo. Every buffer ends right before a PAGE_NOACCESS page, so a
// read past its end faults and fails the test instead of passing unnoticed.
TEST_CLASS(NtfsSecurityDescriptorEntryTest)
{
    static const size_t kEntryHeaderSize = FIELD_OFFSET(SECURITY_DESCRIPTOR_ENTRY, SecurityDescriptor);

    // Two pages, the second one inaccessible; Place() copies bytes to the very end of the first.
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

        BufferView Place(const std::vector<BYTE>& bytes)
        {
            Assert::IsTrue(bytes.size() <= m_pageSize);
            BYTE* start = m_base + m_pageSize - bytes.size();
            std::copy(bytes.begin(), bytes.end(), start);
            return BufferView(start, bytes.size());
        }

    private:
        BYTE* m_base = nullptr;
        size_t m_pageSize = 0;
    };

    static std::vector<BYTE> MakeDescriptor(const wchar_t* sddl)
    {
        PSECURITY_DESCRIPTOR sd = nullptr;
        ULONG size = 0;
        Assert::IsTrue(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, &size) != FALSE);
        std::vector<BYTE> bytes(static_cast<BYTE*>(sd), static_cast<BYTE*>(sd) + size);
        LocalFree(sd);
        return bytes;
    }

    // An $SDS stream holding 'padding' bytes, then one entry wrapping 'descriptor', ending the stream.
    static std::vector<BYTE> MakeSds(const std::vector<BYTE>& descriptor, size_t padding)
    {
        std::vector<BYTE> sds(padding + kEntryHeaderSize + descriptor.size(), 0);
        SECURITY_DESCRIPTOR_ENTRY* entry = reinterpret_cast<SECURITY_DESCRIPTOR_ENTRY*>(sds.data() + padding);
        entry->Hash = 0x12345678;
        entry->SecID = 0x100;
        entry->OffsetEntry = padding;
        entry->SizeEntry = static_cast<UINT32>(kEntryHeaderSize + descriptor.size());
        std::copy(descriptor.begin(), descriptor.end(), sds.begin() + padding + kEntryHeaderSize);
        return sds;
    }

    static SECURITY_DESCRIPTOR_RELATIVE* Header(std::vector<BYTE>& sds, size_t padding)
    {
        return &reinterpret_cast<SECURITY_DESCRIPTOR_ENTRY*>(sds.data() + padding)->SecurityDescriptor;
    }

    static const wchar_t* Sddl() { return L"O:BAG:SYD:(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)S:(AU;SAFA;FA;;;WD)"; }

    // What NTFSInfo does with an accepted entry: none of it may fault.
    static void Consume(BufferView sds, size_t offset)
    {
        const SECURITY_DESCRIPTOR_ENTRY* entry = reinterpret_cast<const SECURITY_DESCRIPTOR_ENTRY*>(sds.data() + offset);
        PSECURITY_DESCRIPTOR sd = const_cast<PSECURITY_DESCRIPTOR>(static_cast<LPCVOID>(&entry->SecurityDescriptor));
        LPWSTR text = nullptr;
        if (ConvertSecurityDescriptorToStringSecurityDescriptorW(
                sd,
                SDDL_REVISION_1,
                OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION
                    | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                &text,
                nullptr))
        {
            LocalFree(text);
        }

        std::ignore = GetSecurityDescriptorLength(sd);
    }

public:
    TEST_METHOD(AcceptsAWellFormedEntry)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> descriptor = MakeDescriptor(Sddl());
        for (const size_t padding : {size_t(0), size_t(0x40)})
        {
            BufferView sds = guard.Place(MakeSds(descriptor, padding));
            Assert::IsTrue(Ntfs::IsSecurityDescriptorEntryValid(sds, padding));
            Consume(sds, padding);
        }
    }

    TEST_METHOD(RefusesAnEntryOutsideTheStream)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> sdsBytes = MakeSds(MakeDescriptor(Sddl()), 0x40);
        BufferView sds = guard.Place(sdsBytes);

        for (const uint64_t offset :
             {uint64_t(sdsBytes.size()), uint64_t(sdsBytes.size() - 1), uint64_t(sdsBytes.size() - kEntryHeaderSize),
              uint64_t(0xFFFFFFFF), uint64_t(0xFFFFFFFFFFFFFFFF)})
        {
            Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(sds, offset));
        }

        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(BufferView(), 0));
    }

    // SizeEntry must cover the header and the descriptor, and stay inside the stream. A descriptor whose parts lie
    // past its own SizeEntry is refused even when those bytes exist in the stream (the next entry's).
    TEST_METHOD(RefusesAnEntrySizeThatDoesNotHoldTheDescriptor)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> descriptor = MakeDescriptor(Sddl());
        std::vector<BYTE> sdsBytes = MakeSds(descriptor, 0);
        SECURITY_DESCRIPTOR_ENTRY* entry = reinterpret_cast<SECURITY_DESCRIPTOR_ENTRY*>(sdsBytes.data());
        const UINT32 fullSize = entry->SizeEntry;

        for (const UINT32 size : {UINT32(0), UINT32(kEntryHeaderSize), fullSize + 1, UINT32(0xFFFFFFFF)})
        {
            entry->SizeEntry = size;
            Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(sdsBytes), 0));
        }

        for (UINT32 size = UINT32(kEntryHeaderSize + sizeof(SECURITY_DESCRIPTOR_RELATIVE)); size < fullSize; ++size)
        {
            entry->SizeEntry = size;
            Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(sdsBytes), 0), std::to_wstring(size).c_str());
        }
    }

    TEST_METHOD(RefusesADescriptorThatIsNotSelfRelative)
    {
        GuardedBuffer guard;
        std::vector<BYTE> sdsBytes = MakeSds(MakeDescriptor(Sddl()), 0);

        std::vector<BYTE> wrongRevision = sdsBytes;
        Header(wrongRevision, 0)->Revision = 2;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(wrongRevision), 0));

        std::vector<BYTE> absolute = sdsBytes;
        Header(absolute, 0)->Control &= ~SE_SELF_RELATIVE;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(absolute), 0));
    }

    // Owner, group, SACL and DACL offsets pointing at or past the end of the descriptor, or at a structure that does
    // not fit, are refused.
    TEST_METHOD(RefusesComponentsOutsideTheDescriptor)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> descriptor = MakeDescriptor(Sddl());
        const std::vector<BYTE> sdsBytes = MakeSds(descriptor, 0);
        const DWORD size = static_cast<DWORD>(descriptor.size());

        for (int field = 0; field < 4; ++field)
        {
            for (const DWORD offset : {size, size - 1, size - 4, DWORD(0xFFFFFFFF)})
            {
                std::vector<BYTE> forged = sdsBytes;
                SECURITY_DESCRIPTOR_RELATIVE* header = Header(forged, 0);
                DWORD* fields[] = {&header->Owner, &header->Group, &header->Sacl, &header->Dacl};
                *fields[field] = offset;
                const std::wstring what = std::to_wstring(field) + L"@" + std::to_wstring(offset);
                Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(forged), 0), what.c_str());
            }
        }
    }

    TEST_METHOD(RefusesAForgedSidOrAcl)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> sdsBytes = MakeSds(MakeDescriptor(Sddl()), 0);
        const size_t sdStart = kEntryHeaderSize;

        std::vector<BYTE> sid = sdsBytes;
        reinterpret_cast<SID*>(sid.data() + sdStart + Header(sid, 0)->Owner)->SubAuthorityCount = 0xFF;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(sid), 0));

        std::vector<BYTE> aclTooLarge = sdsBytes;
        reinterpret_cast<ACL*>(aclTooLarge.data() + sdStart + Header(aclTooLarge, 0)->Dacl)->AclSize = 0xFFFF;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(aclTooLarge), 0));

        std::vector<BYTE> aclTooSmall = sdsBytes;
        reinterpret_cast<ACL*>(aclTooSmall.data() + sdStart + Header(aclTooSmall, 0)->Dacl)->AclSize = 4;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(aclTooSmall), 0));

        std::vector<BYTE> aceTooLarge = sdsBytes;
        const size_t firstAce = sdStart + Header(aceTooLarge, 0)->Dacl + sizeof(ACL);
        reinterpret_cast<ACE_HEADER*>(aceTooLarge.data() + firstAce)->AceSize = 0xFFFF;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(aceTooLarge), 0));

        std::vector<BYTE> aceSid = sdsBytes;
        const size_t aceSidOffset = sdStart + Header(aceSid, 0)->Dacl + sizeof(ACL) + FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
        reinterpret_cast<SID*>(aceSid.data() + aceSidOffset)->SubAuthorityCount = 15;
        Assert::IsFalse(Ntfs::IsSecurityDescriptorEntryValid(guard.Place(aceSid), 0));
    }

    // Every single-byte corruption of the entry: whatever is accepted is then consumed as NTFSInfo does, against the
    // guard page. A validator missing a bound makes the consumer fault here.
    TEST_METHOD(AnyAcceptedCorruptionIsSafeToConsume)
    {
        GuardedBuffer guard;
        const std::vector<BYTE> sdsBytes = MakeSds(MakeDescriptor(Sddl()), 0);
        const BYTE values[] = {0x00, 0x01, 0x04, 0x0F, 0x10, 0x7F, 0x80, 0xFE, 0xFF};

        for (size_t i = 0; i < sdsBytes.size(); ++i)
        {
            for (const BYTE value : values)
            {
                std::vector<BYTE> forged = sdsBytes;
                forged[i] = value;
                BufferView sds = guard.Place(forged);
                if (Ntfs::IsSecurityDescriptorEntryValid(sds, 0))
                {
                    Consume(sds, 0);
                }
            }
        }
    }
};

}  // namespace Orc::Test
