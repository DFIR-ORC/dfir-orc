//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "EvtLibrary.h"

using namespace Orc;

HRESULT EvtLibrary::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_EvtQuery, "EvtQuery");
        Get(m_EvtNext, "EvtNext");
        Get(m_EvtCreateRenderContext, "EvtCreateRenderContext");
        Get(m_EvtRender, "EvtRender");
        Get(m_EvtClose, "EvtClose");

        m_bInitialized = true;
    }
    return S_OK;
}

EvtLibrary::~EvtLibrary() {}
