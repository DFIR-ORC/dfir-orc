//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "MSIExtension.h"

using namespace Orc;

HRESULT MSIExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_MsiGetFileSignatureInformation, "MsiGetFileSignatureInformationW");

        m_bInitialized = true;
    }
    return S_OK;
}

MSIExtension::~MSIExtension() {}
