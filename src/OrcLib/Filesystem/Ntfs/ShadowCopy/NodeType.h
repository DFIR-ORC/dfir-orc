//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <system_error>

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

enum class NodeType : uint32_t
{
    kUnknown = 0,
    kVolumeHeader = 1,
    kCatalog = 2,
    kDiffAreaTable = 3,
    kApplicationInfo = 4,
    kDiffAreaLocationTable = 5,
    kDiffAreaBitmap = 6
};

std::string_view ToString(NodeType type);

NodeType ToNodeType(const std::string& value, std::error_code& ec);
NodeType ToNodeType(const uint32_t value, std::error_code& ec);

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
