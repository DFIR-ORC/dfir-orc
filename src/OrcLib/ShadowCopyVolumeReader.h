//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "SnapshotVolumeReader.h"
#include "VolumeShadowCopies.h"
#include "Filesystem/Ntfs/ShadowCopy/ShadowCopyStream.h"
#include "Stream/VolumeStreamReader.h"

//
// This is another implementation of SnapshotVolumeReader which does not rely on Microsoft api to access volume shadow
// copies.
//

namespace Orc {

class ShadowCopyVolumeReader : public SnapshotVolumeReader
{
public:
    ShadowCopyVolumeReader(const VolumeShadowCopies::Shadow& shadow)
        : SnapshotVolumeReader(shadow)
    {
    }

    void Accept(VolumeReaderVisitor& visitor) const override { SnapshotVolumeReader::Accept(visitor); }

    HRESULT LoadDiskProperties() override;

    HRESULT Seek(ULONGLONG offset) override;
    HRESULT Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead) override;
    HRESULT Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead) override;

    std::shared_ptr<VolumeReader> ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags) override;

private:
    Ntfs::ShadowCopy::ShadowCopyStream::Ptr m_stream;
    std::shared_ptr<VolumeReader> m_volume;
};

}  // namespace Orc
