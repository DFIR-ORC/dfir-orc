//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <string_view>
#include <system_error>

#include "Utils/EnumFlags.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

//
// Details on kForwarder value:
//
// From https://github.com/msuhanov/dfir_ntfs/blob/master/dfir_ntfs/ShadowCopy.py
//
// The 'StoreEntry::Layout::dataRelativeOffset' field contains the offset to be resolved using the next store (the
// forward offset). This entry also affects a subsequent (not necessary the next one) regular entry (from the same
// store, if any) having its original volume offset equal to the forward offset: the original volume offset of such an
// entry is replaced with the original volume offset of the forwarder entry (this affects only one regular entry).
//

enum class DiffAreaTableEntryFlags : uint32_t
{
    kForwarder = 0x1,
    kOverlay = 0x2,
    kUnused = 0x4,
    kUnknown8 = 0x8,
    kUnknown10 = 0x10,  // pre-copy
    kUnknown20 = 0x20,
    kUnknown40 = 0x40,
    kUnknown80 = 0x80
};

std::string ToString(DiffAreaTableEntryFlags attributes);

DiffAreaTableEntryFlags ToDiffAreaEntryFlags(const uint32_t flags, std::error_code& ec);

}  // namespace ShadowCopy
}  // namespace Ntfs

ENABLE_BITMASK_OPERATORS(Orc::Ntfs::ShadowCopy::DiffAreaTableEntryFlags)

}  // namespace Orc
