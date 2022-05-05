//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "CatalogEntryType.h"

#include <map>

namespace {

constexpr std::string_view kUnknown = "<unknown>";
constexpr std::string_view kCatalogEnd = "catalog_end";
constexpr std::string_view kFreeEntry = "free_entry";
constexpr std::string_view kSnapshotInfo = "snapshot_info";
constexpr std::string_view kDiffAreaInfo = "diff_area_info";

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

std::string_view ToString(CatalogEntryType type)
{
    switch (type)
    {
        case CatalogEntryType::kUnknown:
            return kUnknown;
        case CatalogEntryType::kCatalogEnd:
            return kCatalogEnd;
        case CatalogEntryType::kFreeEntry:
            return kFreeEntry;
        case CatalogEntryType::kSnapshotInfo:
            return kSnapshotInfo;
        case CatalogEntryType::kDiffAreaInfo:
            return kDiffAreaInfo;
    }

    return kUnknown;
}

CatalogEntryType ToCatalogEntryType(const std::string& value, std::error_code& ec)
{
    const std::map<std::string_view, CatalogEntryType> map = {
        {kUnknown, CatalogEntryType::kUnknown},
        {kCatalogEnd, CatalogEntryType::kCatalogEnd},
        {kFreeEntry, CatalogEntryType::kFreeEntry},
        {kSnapshotInfo, CatalogEntryType::kSnapshotInfo},
        {kDiffAreaInfo, CatalogEntryType::kDiffAreaInfo}};

    auto it = map.find(value);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return CatalogEntryType::kUnknown;
    }

    return it->second;
}

CatalogEntryType ToCatalogEntryType(const uint32_t type, std::error_code& ec)
{
    switch (static_cast<CatalogEntryType>(type))
    {
        case CatalogEntryType::kUnknown:
            return CatalogEntryType::kUnknown;
        case CatalogEntryType::kCatalogEnd:
            return CatalogEntryType::kCatalogEnd;
        case CatalogEntryType::kFreeEntry:
            return CatalogEntryType::kFreeEntry;
        case CatalogEntryType::kSnapshotInfo:
            return CatalogEntryType::kSnapshotInfo;
        case CatalogEntryType::kDiffAreaInfo:
            return CatalogEntryType::kDiffAreaInfo;
        default:
            ec = std::make_error_code(std::errc::invalid_argument);
            return CatalogEntryType::kUnknown;
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
