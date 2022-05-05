//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "VolumeSnapshotAttributes.h"

#include <tuple>

#include "Log/Log.h"
#include "Utils/String.h"

namespace {

constexpr std::string_view kUnknown = "<unknown>";
constexpr std::string_view kNone = "<none>";

constexpr std::string_view kPersistent = "persistent";
constexpr std::string_view kNoAutoRecovery = "no_auto_recovery";
constexpr std::string_view kClientAccessible = "client_accessible";
constexpr std::string_view kNoAutoRelease = "no_auto_release";
constexpr std::string_view kNoWriters = "no_writers";
constexpr std::string_view kTransportable = "transportable";
constexpr std::string_view kNotSurfaced = "not_surfaced";
constexpr std::string_view kNotTransacted = "not_transacted";
constexpr std::string_view kHardwareAssisted = "hardware_assisted";
constexpr std::string_view kDifferential = "differential";
constexpr std::string_view kPlex = "plex";
constexpr std::string_view kImported = "imported";
constexpr std::string_view kExposedLocally = "exposed_locally";
constexpr std::string_view kExposedRemotely = "exposed_remotely";
constexpr std::string_view kAutoRecover = "auto_recover";
constexpr std::string_view kRollbackRecovery = "rollback_recovery";
constexpr std::string_view kDelayedPostsnapshot = "delayed_post_snapshot";
constexpr std::string_view kTxfRecovery = "txf_recovery";
constexpr std::string_view kFileShare = "file_share";

constexpr std::array kVolumeSnapshotAttributes = {
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kPersistent, kPersistent},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kNoAutoRecovery, kNoAutoRecovery},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kClientAccessible, kClientAccessible},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kNoAutoRelease, kNoAutoRelease},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kNoWriters, kNoWriters},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kTransportable, kTransportable},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kNotSurfaced, kNotSurfaced},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kNotTransacted, kNotTransacted},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kHardwareAssisted, kHardwareAssisted},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kDifferential, kDifferential},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kPlex, kPlex},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kImported, kImported},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kExposedLocally, kExposedLocally},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kExposedRemotely, kExposedRemotely},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kAutorecover, kAutoRecover},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kRollbackRecovery, kRollbackRecovery},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kDelayedPostsnapshot, kDelayedPostsnapshot},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kTxfRecovery, kTxfRecovery},
    std::tuple {Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes::kFileShare, kFileShare}};

bool HasInvalidFlag(uint32_t flags)
{
    using namespace Orc::Ntfs::ShadowCopy;
    using namespace Orc;

    auto remainingFlags = flags;
    for (const auto& [flag, description] : kVolumeSnapshotAttributes)
    {
        if (HasFlag(static_cast<VolumeSnapshotAttributes>(flags), flag))
        {
            remainingFlags -= static_cast<std::underlying_type_t<VolumeSnapshotAttributes>>(flag);
        }
    }

    if (remainingFlags)
    {
        return true;
    }

    return false;
}

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

std::string ToString(VolumeSnapshotAttributes flags)
{
    if (flags == static_cast<VolumeSnapshotAttributes>(0))
    {
        return std::string(kNone);
    }

    if (::HasInvalidFlag(std::underlying_type_t<VolumeSnapshotAttributes>(flags)))
    {
        Log::Debug(
            "Failed to convert some flags to string (value: {})",
            std::underlying_type_t<VolumeSnapshotAttributes>(flags));

        return fmt::format("{:#x}", std::underlying_type_t<VolumeSnapshotAttributes>(flags));
    }

    std::vector<std::string_view> strings;

    for (const auto& [flag, description] : kVolumeSnapshotAttributes)
    {
        if (HasFlag(flags, flag))
        {
            strings.push_back(description);
        }
    }

    std::string output;
    Join(std::cbegin(strings), std::cend(strings), output, '|');
    return output;
}

VolumeSnapshotAttributes ToVolumeSnapshotAttributes(const uint32_t attributes, std::error_code& ec)
{
    if (::HasInvalidFlag(std::underlying_type_t<VolumeSnapshotAttributes>(attributes)))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }

    return static_cast<VolumeSnapshotAttributes>(attributes);
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
