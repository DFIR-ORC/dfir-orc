//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "Kernel32Extension.h"

#include <concrt.h>

using namespace Orc;

HRESULT Kernel32Extension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Try(m_InitializeProcThreadAttributeList, "InitializeProcThreadAttributeList");
        Try(m_UpdateProcThreadAttribute, "UpdateProcThreadAttribute");
        Try(m_DeleteProcThreadAttributeList, "DeleteProcThreadAttributeList");
        Try(m_CancelIoEx, "CancelIoEx");
        Try(m_EnumResourceNamesExW, "EnumResourceNamesExW");
        Try(m_EnumResourceNamesW, "EnumResourceNamesW");
        Try(m_ReOpenFile, "ReOpenFile");
        m_bInitialized = true;
    }
    return S_OK;
}
