//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <system_error>

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

enum class CatalogEntryType : uint32_t
{
    kCatalogEnd = 0x0,
    kFreeEntry = 0x1,
    kSnapshotInfo = 0x2,
    kDiffAreaInfo = 0x3,
    kUnknown = 0xFFFFFFFF,
};

std::string_view ToString(CatalogEntryType type);

CatalogEntryType ToCatalogEntryType(const std::string& value, std::error_code& ec);
CatalogEntryType ToCatalogEntryType(const uint32_t value, std::error_code& ec);

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
