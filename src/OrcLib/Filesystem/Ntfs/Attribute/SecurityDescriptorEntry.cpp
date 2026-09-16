//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Filesystem/Ntfs/Attribute/SecurityDescriptorEntry.h"

#include <cstddef>

#include "Log/Log.h"
#include "NtfsDataStructures.h"

namespace {

using Orc::BufferView;

constexpr size_t kEntryHeaderSize = offsetof(SECURITY_DESCRIPTOR_ENTRY, SecurityDescriptor);
constexpr size_t kMinimumEntrySize = kEntryHeaderSize + sizeof(SECURITY_DESCRIPTOR_RELATIVE);
constexpr size_t kSidHeaderSize = offsetof(SID, SubAuthority);

// Overflow-safe: 'size' bytes at 'offset' fit in a buffer of 'bufferSize' bytes.
constexpr bool IsRangeValid(uint64_t bufferSize, uint64_t offset, uint64_t size)
{
    return offset <= bufferSize && size <= bufferSize - offset;
}

// The T at 'offset' in 'buffer' when 'size' bytes (sizeof(T) by default) fit there, else nullptr.
template <typename T>
const T* Peek(BufferView buffer, uint64_t offset, uint64_t size = sizeof(T))
{
    if (!IsRangeValid(buffer.size(), offset, size))
    {
        return nullptr;
    }

    return reinterpret_cast<const T*>(buffer.data() + offset);
}

// A SID at 'offset' in a self-relative descriptor: its fixed header, then as many sub-authorities as it declares.
bool IsSidInBounds(BufferView descriptor, uint64_t offset)
{
    const SID* sid = Peek<SID>(descriptor, offset, kSidHeaderSize);
    if (sid == nullptr || sid->Revision != SID_REVISION || sid->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES)
    {
        return false;
    }

    return IsRangeValid(descriptor.size(), offset, kSidHeaderSize + uint64_t(sid->SubAuthorityCount) * sizeof(DWORD));
}

// An ACL at 'offset': its header and the AclSize it declares must fit, then IsValidAcl checks every ACE (and the
// SID inside it) against that AclSize.
bool IsAclInBounds(BufferView descriptor, uint64_t offset)
{
    const ACL* acl = Peek<ACL>(descriptor, offset);
    if (acl == nullptr || acl->AclSize < sizeof(ACL) || !IsRangeValid(descriptor.size(), offset, acl->AclSize))
    {
        return false;
    }

    return IsValidAcl(const_cast<PACL>(acl)) != FALSE;
}

}  // namespace

namespace Orc {
namespace Ntfs {

bool IsSecurityDescriptorEntryValid(BufferView sds, uint64_t offset)
{
    const SECURITY_DESCRIPTOR_ENTRY* entry = Peek<SECURITY_DESCRIPTOR_ENTRY>(sds, offset, kMinimumEntrySize);
    if (entry == nullptr)
    {
        Log::Debug(
            "$SDS: security descriptor entry out of bounds (offset: {:#x}, $SDS size: {:#x})", offset, sds.size());
        return false;
    }

    if (entry->SizeEntry < kMinimumEntrySize || !IsRangeValid(sds.size(), offset, entry->SizeEntry))
    {
        Log::Debug(
            "$SDS: security descriptor entry size out of bounds (offset: {:#x}, size: {:#x})",
            offset,
            entry->SizeEntry);
        return false;
    }

    // Everything below is bounded by the entry's own size, not by $SDS: a descriptor may not borrow the next entry.
    const BufferView descriptor = sds.subspan(offset, entry->SizeEntry).subspan(kEntryHeaderSize);
    const SECURITY_DESCRIPTOR_RELATIVE& header = entry->SecurityDescriptor;

    if (header.Revision != SECURITY_DESCRIPTOR_REVISION || (header.Control & SE_SELF_RELATIVE) == 0)
    {
        Log::Debug(
            "$SDS: not a self-relative security descriptor (offset: {:#x}, revision: {}, control: {:#x})",
            offset,
            header.Revision,
            header.Control);
        return false;
    }

    // A zero offset means "absent". Any other offset is checked even without its *_PRESENT flag, so no reader can be
    // led outside the entry whichever flags it honours.
    for (const DWORD sidOffset : {header.Owner, header.Group})
    {
        if (sidOffset != 0 && !IsSidInBounds(descriptor, sidOffset))
        {
            Log::Debug("$SDS: owner or group SID out of bounds (offset: {:#x}, SID offset: {:#x})", offset, sidOffset);
            return false;
        }
    }

    for (const DWORD aclOffset : {header.Sacl, header.Dacl})
    {
        if (aclOffset != 0 && !IsAclInBounds(descriptor, aclOffset))
        {
            Log::Debug(
                "$SDS: SACL or DACL out of bounds or malformed (offset: {:#x}, ACL offset: {:#x})", offset, aclOffset);
            return false;
        }
    }

    return true;
}

}  // namespace Ntfs
}  // namespace Orc
