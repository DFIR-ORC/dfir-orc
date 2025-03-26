//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
//
// See: c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include\vss.h
//

#pragma once

#include <string>
#include <system_error>

#include "Utils/EnumFlags.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

enum class VolumeSnapshotAttributes : uint32_t
{
    kPersistent = 0x1,
    kNoAutoRecovery = 0x2,
    kClientAccessible = 0x4,
    kNoAutoRelease = 0x8,
    kNoWriters = 0x10,
    kTransportable = 0x20,
    kNotSurfaced = 0x40,
    kNotTransacted = 0x80,
    kHardwareAssisted = 0x10000,
    kDifferential = 0x20000,
    kPlex = 0x40000,
    kImported = 0x80000,
    kExposedLocally = 0x100000,
    kExposedRemotely = 0x200000,
    kAutorecover = 0x400000,
    kRollbackRecovery = 0x0800000,
    kDelayedPostsnapshot = 0x1000000,
    kTxfRecovery = 0x2000000,
    kFileShare = 0x4000000
};

std::string ToString(VolumeSnapshotAttributes attributes);

VolumeSnapshotAttributes ToVolumeSnapshotAttributes(const uint32_t attributes, std::error_code& ec);

}  // namespace ShadowCopy
}  // namespace Ntfs

ENABLE_BITMASK_OPERATORS(Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes)

}  // namespace Orc
