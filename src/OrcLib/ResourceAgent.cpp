//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "psapi.h"

#include "ResourceAgent.h"
#include "ParameterCheck.h"
#include "LogFileWriter.h"

#include "RunningProcesses.h"

using namespace std;

HRESULT ResourceAgent::OpenForReadOnly(const HMODULE hModule, const HRSRC hRes)
{
    HRESULT hr = E_FAIL;

    // First find and load the required resource

    m_hResource = hRes;
    m_hModule = hModule;

    if ((m_hFileResource = LoadResource(hModule, m_hResource)) == NULL)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;

    if ((lpFile = LockResource(m_hFileResource)) == NULL)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(m_hModule, m_hResource)) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return MemoryAgent::OpenForReadOnly(lpFile, dwSize);
}

HRESULT ResourceAgent::Close()
{
    MemoryAgent::Close();
    if (m_hFileResource != NULL)
        FreeResource(m_hFileResource);
    if (m_hModule)
    {
        FreeLibrary(m_hModule);
        m_hModule = NULL;
    }
    return S_OK;
}

ResourceAgent::~ResourceAgent(void)
{
    Close();
}
