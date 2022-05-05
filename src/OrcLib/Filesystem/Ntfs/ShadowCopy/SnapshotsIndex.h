//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Utils/Result.h"
#include "Stream/StreamReader.h"

#include "Filesystem/Ntfs/ShadowCopy/SnapshotInformation.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class SnapshotsIndexHeader;

class SnapshotsIndex final
{
public:
    static void Parse(StreamReader& stream, SnapshotsIndex& index, std::error_code& ec);

    Result<const SnapshotInformation*> Get(const GUID& snapshotId) const;
    Result<const SnapshotInformation*> Get(const std::string& snapshotId) const;

    const std::vector<SnapshotInformation>& Items() const;

private:
    std::vector<SnapshotInformation> m_items;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
