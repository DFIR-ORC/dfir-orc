//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Filesystem/Ntfs/ShadowCopy/Catalog.h"
#include "Filesystem/Ntfs/ShadowCopy/ApplicationInformation.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class SnapshotInformation final
{
public:
    SnapshotInformation()
        : m_snapshotInfo()
        , m_diffAreaInfo()
        , m_applicationInfo()
    {
    }

    SnapshotInformation(
        CatalogEntry::SnapshotInfo snapshotInfo,
        CatalogEntry::DiffAreaInfo diffAreaInfo,
        ApplicationInformation::VssLocalInfo applicationInfo)
        : m_snapshotInfo(std::move(snapshotInfo))
        , m_diffAreaInfo(std::move(diffAreaInfo))
        , m_applicationInfo(std::move(applicationInfo))
    {
    }

    const GUID& Guid() const { return m_snapshotInfo.Guid(); }
    uint64_t Size() const { return m_snapshotInfo.Size(); }
    uint64_t LayerPosition() const { return m_snapshotInfo.Position(); }
    const FILETIME& CreationTime() const { return m_snapshotInfo.CreationTime(); }
    uint64_t Flags() const { return m_snapshotInfo.Flags(); }

    const GUID& ShadowCopySetId() const { return m_applicationInfo.ShadowCopySetId(); }
    const GUID& ShadowCopyId() const { return m_applicationInfo.ShadowCopyId(); }
    const std::wstring& MachineString() const { return m_applicationInfo.MachineString(); }
    const std::wstring& ServiceString() const { return m_applicationInfo.ServiceString(); }
    VolumeSnapshotAttributes VolumeSnapshotAttributes() const { return m_applicationInfo.VolumeSnapshotAttributes(); }
    SnapshotContext SnapshotContext() const { return m_applicationInfo.SnapshotContext(); }

    uint64_t DiffAreaTableOffset() const { return m_diffAreaInfo.FirstDiffAreaTableOffset(); }
    uint64_t BitmapOffset() const { return m_diffAreaInfo.FirstBitmapOffset(); }
    uint64_t PreviousBitmapOffset() const { return m_diffAreaInfo.PreviousBitmapOffset(); }

    const CatalogEntry::DiffAreaInfo& DiffAreaInfo() const { return m_diffAreaInfo; }
    const ApplicationInformation::VssLocalInfo& ApplicationInformation() const { return m_applicationInfo; }

private:
    CatalogEntry::SnapshotInfo m_snapshotInfo;
    CatalogEntry::DiffAreaInfo m_diffAreaInfo;
    ApplicationInformation::VssLocalInfo m_applicationInfo;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
