//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "CompleteVolumeReader.h"
#include "VolumeShadowCopies.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API SnapshotVolumeReader : public CompleteVolumeReader
{
private:
    VolumeShadowCopies::Shadow m_Shadow;

protected:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

public:
    SnapshotVolumeReader(logger pLog, const VolumeShadowCopies::Shadow& Snapshot);

    const WCHAR* ShortVolumeName() { return L"\\"; };

    virtual HRESULT LoadDiskProperties(void);
    virtual HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }

    const GUID& GetSnapshotID() const { return m_Shadow.guid; }

    ~SnapshotVolumeReader(void);
};

}  // namespace Orc

constexpr auto REGEX_SNAPSHOT = L"\\\\\\\\\\?\\\\GLOBALROOT\\\\Device\\\\HarddiskVolumeShadowCopy([0-9]+)";
constexpr auto REGEX_SNAPSHOT_NUM = 1;

#pragma managed(pop)
