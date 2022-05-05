//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
//
// See: c:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include\vss.h
//
#pragma once

#include <string>

#include "VolumeSnapshotAttributes.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

enum class SnapshotContext : uint32_t
{
    kBackup = 0,

    kFileShareBackup = static_cast<uint32_t>(VolumeSnapshotAttributes::kNoWriters),

    kNasRollback = static_cast<uint32_t>(
        VolumeSnapshotAttributes::kPersistent | VolumeSnapshotAttributes::kNoAutoRelease
        | VolumeSnapshotAttributes::kNoWriters),

    kAppRollback =
        static_cast<uint32_t>(VolumeSnapshotAttributes::kPersistent | VolumeSnapshotAttributes::kNoAutoRelease),

    kClientAccessible = static_cast<uint32_t>(
        VolumeSnapshotAttributes::kPersistent | VolumeSnapshotAttributes::kClientAccessible
        | VolumeSnapshotAttributes::kNoAutoRelease | VolumeSnapshotAttributes::kNoWriters),

    kClientAccessibleWriters = static_cast<uint32_t>(
        VolumeSnapshotAttributes::kPersistent | VolumeSnapshotAttributes::kClientAccessible
        | VolumeSnapshotAttributes::kNoAutoRelease),

    kAll = 0xffffffff
};

std::string_view ToString(SnapshotContext context);

SnapshotContext ToSnapshotContext(const uint32_t context, std::error_code& ec);

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
