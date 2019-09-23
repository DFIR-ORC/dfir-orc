//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "TableOutputExtension.h"

using namespace Orc;

HRESULT Orc::TableOutputExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Try(m_ConnectionFactory, "ConnectionFactory");
        Try(m_ConnectTableFactory, "ConnectTableFactory");
        Try(m_StreamTableFactory, "StreamTableFactory");
        m_bInitialized = true;
    }

    return S_OK;
}

Orc::TableOutputExtension::~TableOutputExtension() {}
