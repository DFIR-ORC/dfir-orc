//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OutputWriter.h"
#include "VssAPIExtension.h"

#pragma warning(disable : 4091)
#include <vss.h>
#pragma warning(default : 4091)

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;
class Location;

class ORCLIB_API VolumeShadowCopies
{
public:
    static const FlagsDefinition g_VssFlags[];

    class Shadow
    {
    public:
        std::wstring VolumeName;
        std::wstring DeviceInstance;
        VSS_VOLUME_SNAPSHOT_ATTRIBUTES Attributes;
        VSS_TIMESTAMP CreationTime;
        GUID guid;

        Shadow(
            LPCWSTR szVolume,
            LPCWSTR szDevice,
            const VSS_VOLUME_SNAPSHOT_ATTRIBUTES attrs,
            const VSS_TIMESTAMP time,
            const GUID& id)
            : VolumeName(szVolume)
            , DeviceInstance(szDevice)
            , Attributes(attrs)
            , CreationTime(time)
            , guid(id) {};
    };

private:
    logger _L_;
    std::shared_ptr<VssAPIExtension> m_vssapi;

public:
    VolumeShadowCopies(logger pLog)
        : _L_(std::move(pLog))
    {
    }

    HRESULT EnumerateShadows(std::vector<Shadow>& shadows);

    ~VolumeShadowCopies();
};
}  // namespace Orc

#pragma managed(pop)
