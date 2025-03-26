//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "CompleteVolumeReader.h"
#include "VolumeShadowCopies.h"

#pragma managed(push, off)

namespace Orc {

class SnapshotVolumeReader : public CompleteVolumeReader
{
protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    SnapshotVolumeReader(const VolumeShadowCopies::Shadow& Snapshot);

    void Accept(VolumeReaderVisitor& visitor) const override { return visitor.Visit(*this); }

    const WCHAR* ShortVolumeName() { return L"\\"; };

    virtual HRESULT LoadDiskProperties(void);
    virtual HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }

    const GUID& GetSnapshotID() const { return m_Shadow.guid; }

    ~SnapshotVolumeReader(void);

    HRESULT Read(ULONGLONG offset, CBinaryBuffer& buffer, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead) override;

protected:
    VolumeShadowCopies::Shadow m_Shadow;
};

}  // namespace Orc

constexpr auto REGEX_SNAPSHOT = L"\\\\\\\\\\?\\\\GLOBALROOT\\\\Device\\\\HarddiskVolumeShadowCopy([0-9]+)";
constexpr auto REGEX_SNAPSHOT_NUM = 1;

#pragma managed(pop)
