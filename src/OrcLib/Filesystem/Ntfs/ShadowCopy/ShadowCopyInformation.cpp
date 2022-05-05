//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ShadowCopyInformation.h"

#include "Filesystem/Ntfs/ShadowCopy/SnapshotInformation.h"

using namespace Orc::Ntfs::ShadowCopy;
using namespace Orc::Ntfs;
using namespace Orc;

ShadowCopyInformation::ShadowCopyInformation()
    : m_shadowCopyId()
    , m_shadowCopySetId()
    , m_size()
    , m_creationTime()
    , m_flags()
    , m_machine()
    , m_service()
    , m_volumeSnapshotAttributes()
    , m_overlayCount()
    , m_copyOnWriteCount()
    , m_forwarderCount()
{
}

ShadowCopyInformation::ShadowCopyInformation(const SnapshotInformation& snapshot)
    : m_shadowCopyId(snapshot.ShadowCopyId())
    , m_shadowCopySetId(snapshot.ShadowCopySetId())
    , m_size(snapshot.Size())
    , m_creationTime(snapshot.CreationTime())
    , m_flags(snapshot.Flags())
    , m_machine(snapshot.MachineString())
    , m_service(snapshot.ServiceString())
    , m_volumeSnapshotAttributes(snapshot.VolumeSnapshotAttributes())
    , m_overlayCount(0)
    , m_copyOnWriteCount(0)
    , m_forwarderCount(0)
{
}

Orc::Ntfs::ShadowCopy::VolumeSnapshotAttributes ShadowCopyInformation::VolumeSnapshotAttributes() const
{
    return m_volumeSnapshotAttributes;
}
