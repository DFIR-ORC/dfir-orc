//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "VssAPIExtension.h"

using namespace Orc;

HRESULT VssAPIExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Try(m_CreateVssBackupComponents, "CreateVssBackupComponents");
        if (m_CreateVssBackupComponents == nullptr)
            Get(m_CreateVssBackupComponents, "CreateVssBackupComponentsInternal");

        Try(m_VssFreeSnapshotProperties, "VssFreeSnapshotProperties");
        if (m_VssFreeSnapshotProperties == nullptr)
            Get(m_VssFreeSnapshotProperties, "VssFreeSnapshotPropertiesInternal");

        if (m_CreateVssBackupComponents == nullptr || m_VssFreeSnapshotProperties == nullptr)
        {
            log::Error(_L_, E_UNEXPECTED, L"Failed to initialize VSSAPI\r\n");
            return E_UNEXPECTED;
        }

        m_bInitialized = true;
    }
    return S_OK;
}

VssAPIExtension::~VssAPIExtension() {}
