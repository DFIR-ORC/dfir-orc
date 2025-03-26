//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "NtDllExtension.h"

#include <concrt.h>

using namespace Orc;

HRESULT NtDllExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_NtOpenFile, "NtOpenFile");
        Get(m_NtOpenDirectoryObject, "NtOpenDirectoryObject");
        Get(m_NtQueryInformationFile, "NtQueryInformationFile");
        Get(m_NtQueryDirectoryFile, "NtQueryDirectoryFile");
        Get(m_NtQueryInformationProcess, "NtQueryInformationProcess");
        Get(m_NtQuerySystemInformation, "NtQuerySystemInformation");
        Get(m_NtQueryObject, "NtQueryObject");
        Get(m_NtQueryDirectoryObject, "NtQueryDirectoryObject");
        Get(m_NtOpenSymbolicLinkObject, "NtOpenSymbolicLinkObject");
        Get(m_NtQuerySymbolicLinkObject, "NtQuerySymbolicLinkObject");
        Try(m_NtSystemDebugControl, "NtSystemDebugControl");
        m_bInitialized = true;
    }
    return S_OK;
}
