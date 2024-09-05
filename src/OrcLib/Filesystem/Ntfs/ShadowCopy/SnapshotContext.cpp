//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "SnapshotContext.h"

#include "Log/Log.h"
#include "Utils/String.h"

namespace {

constexpr std::string_view kUnknown = "<unknown>";
constexpr std::string_view kBackup = "VSS_CTX_BACKUP";
constexpr std::string_view kFileShareBackup = "VSS_CTX_FILE_SHARE_BACKUP";
constexpr std::string_view kNasRollback = "VSS_CTX_NAS_ROLLBACK";
constexpr std::string_view kAppRollback = "VSS_CTX_APP_ROLLBACK";
constexpr std::string_view kClientAccessible = "VSS_CTX_CLIENT_ACCESSIBLE";
constexpr std::string_view kClientAccessibleWriters = "VSS_CTX_CLIENT_ACCESSIBLE_WRITERS";
constexpr std::string_view kAll = "ALL";

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

std::string_view ToString(SnapshotContext context)
{
    switch (context)
    {
        case SnapshotContext::kBackup:
            return kBackup;
        case SnapshotContext::kFileShareBackup:
            return kFileShareBackup;
        case SnapshotContext::kNasRollback:
            return kNasRollback;
        case SnapshotContext::kAppRollback:
            return kAppRollback;
        case SnapshotContext::kClientAccessible:
            return kClientAccessible;
        case SnapshotContext::kClientAccessibleWriters:
            return kClientAccessibleWriters;
        case SnapshotContext::kAll:
            return kAll;
        default:
            return kUnknown;
    }
}

SnapshotContext ToSnapshotContext(const uint32_t attribute, std::error_code& ec)
{
    const auto attributeEnumClass = static_cast<SnapshotContext>(attribute);

    switch (attributeEnumClass)
    {
        case SnapshotContext::kBackup:
        case SnapshotContext::kFileShareBackup:
        case SnapshotContext::kNasRollback:
        case SnapshotContext::kAppRollback:
        case SnapshotContext::kClientAccessible:
        case SnapshotContext::kClientAccessibleWriters:
        case SnapshotContext::kAll:
            return attributeEnumClass;
        default:
            ec = std::make_error_code(std::errc::invalid_argument);
            return attributeEnumClass;
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
