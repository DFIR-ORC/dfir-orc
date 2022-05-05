// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Filesystem/Ntfs/ShadowCopy/VolumeSnapshotAttributes.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class SnapshotInformation;

class ShadowCopyInformation final
{
public:
    ShadowCopyInformation();
    explicit ShadowCopyInformation(const SnapshotInformation& snapshot);

    const GUID& ShadowCopyId() const { return m_shadowCopyId; }
    const GUID& ShadowCopySetId() const { return m_shadowCopySetId; }
    uint64_t Size() const { return m_size; }
    const FILETIME& CreationTime() const { return m_creationTime; }
    uint64_t Flags() const { return m_flags; }
    const std::wstring& MachineString() const { return m_machine; }
    const std::wstring& ServiceString() const { return m_service; }
    Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes VolumeSnapshotAttributes() const;

    uint64_t OverlayCount() const { return m_overlayCount; }
    void SetOverlayCount(uint64_t value) { m_overlayCount = value; }

    uint64_t CopyOnWriteCount() const { return m_copyOnWriteCount; }
    void SetCopyOnWriteCount(uint64_t value) { m_copyOnWriteCount = value; }

private:
    GUID m_shadowCopyId;
    GUID m_shadowCopySetId;
    uint64_t m_size;
    FILETIME m_creationTime;
    uint64_t m_flags;
    std::wstring m_machine;
    std::wstring m_service;
    uint64_t m_copyOnWriteCount;
    uint64_t m_overlayCount;
    uint64_t m_forwarderCount;
    Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes m_volumeSnapshotAttributes;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
