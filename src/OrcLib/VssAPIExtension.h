//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#pragma warning(disable : 4091)
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#pragma warning(default : 4091)

#pragma managed(push, off)

namespace Orc {

class VssAPIExtension : public ExtensionLibrary
{

private:
    HRESULT(WINAPI* m_CreateVssBackupComponents)(_Out_ IVssBackupComponents** ppBackup) = nullptr;
    HRESULT(WINAPI* m_VssFreeSnapshotProperties)(_In_ VSS_SNAPSHOT_PROP* pProp) = nullptr;

public:
    VssAPIExtension()
        : ExtensionLibrary(L"vssapi", L"vssapi.dll") {};

    STDMETHOD(Initialize)();

    template <typename... Args>
    auto CreateVssBackupComponents(Args&&... args)
    {
        return Call(m_CreateVssBackupComponents, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto VssFreeSnapshotProperties(Args&&... args)
    {
        return Call(m_VssFreeSnapshotProperties, std::forward<Args>(args)...);
    }

    ~VssAPIExtension();
};

}  // namespace Orc

#pragma managed(pop)
