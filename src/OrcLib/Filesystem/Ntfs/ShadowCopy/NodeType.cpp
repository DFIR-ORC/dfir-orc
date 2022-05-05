//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "NodeType.h"

#include <map>

namespace {

constexpr std::string_view kUnknown = "<unknown>";
constexpr std::string_view kVolumeHeader = "volume_header";
constexpr std::string_view kCatalog = "catalog";
constexpr std::string_view kDiffAreaTable = "diff_area_table";
constexpr std::string_view kApplicationInfo = "application_info";
constexpr std::string_view kDiffAreaLocationTable = "diff_area_location_table";
constexpr std::string_view kDiffAreaBitmap = "diff_area_bitmap";

}  // namespace

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

std::string_view ToString(NodeType type)
{
    switch (type)
    {
        case NodeType::kUnknown:
            return kUnknown;
        case NodeType::kVolumeHeader:
            return kVolumeHeader;
        case NodeType::kCatalog:
            return kCatalog;
        case NodeType::kDiffAreaTable:
            return kDiffAreaTable;
        case NodeType::kApplicationInfo:
            return kApplicationInfo;
        case NodeType::kDiffAreaLocationTable:
            return kDiffAreaLocationTable;
        case NodeType::kDiffAreaBitmap:
            return kDiffAreaBitmap;
    }

    return kUnknown;
}

NodeType ToNodeType(const std::string& value, std::error_code& ec)
{
    const std::map<std::string_view, NodeType> map = {
        {kUnknown, NodeType::kUnknown},
        {kVolumeHeader, NodeType::kVolumeHeader},
        {kCatalog, NodeType::kCatalog},
        {kDiffAreaTable, NodeType::kDiffAreaTable},
        {kApplicationInfo, NodeType::kApplicationInfo},
        {kDiffAreaLocationTable, NodeType::kDiffAreaLocationTable},
        {kDiffAreaBitmap, NodeType::kDiffAreaBitmap}};

    auto it = map.find(value);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return NodeType::kUnknown;
    }

    return it->second;
}

NodeType ToNodeType(const uint32_t type, std::error_code& ec)
{
    switch (static_cast<NodeType>(type))
    {
        case NodeType::kUnknown:
            return NodeType::kUnknown;
        case NodeType::kVolumeHeader:
            return NodeType::kVolumeHeader;
        case NodeType::kCatalog:
            return NodeType::kCatalog;
        case NodeType::kDiffAreaTable:
            return NodeType::kDiffAreaTable;
        case NodeType::kApplicationInfo:
            return NodeType::kApplicationInfo;
        case NodeType::kDiffAreaLocationTable:
            return NodeType::kDiffAreaLocationTable;
        case NodeType::kDiffAreaBitmap:
            return NodeType::kDiffAreaBitmap;
        default:
            ec = std::make_error_code(std::errc::invalid_argument);
            return NodeType::kUnknown;
    }
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
