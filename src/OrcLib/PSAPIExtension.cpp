//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "PSAPIExtension.h"

#include <concrt.h>

using namespace Orc;

HRESULT PSAPIExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_EnumProcessModulesEx, "EnumProcessModulesEx");
        Get(m_EnumProcessModules, "EnumProcessModules");
        m_bInitialized = true;
    }
    return S_OK;
}
