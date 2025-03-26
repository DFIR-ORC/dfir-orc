//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "DbgHelpLibrary.h"

#include "SystemDetails.h"

using namespace Orc;

using namespace std::string_literals;

DbgHelpLibrary::DbgHelpLibrary()
    : ExtensionLibrary(L"DbgHelp.dll"s, L"DBGHELP_X86DLL"s, L"DBGHELP_X64DLL"s, L"DBGHELP_ARM64DLL"s)
{
    m_strDesiredName = L"DbgHelp.dll"s;
}

HRESULT DbgHelpLibrary::Initialize()
{
    using namespace std::string_literals;

    HRESULT hr = E_FAIL;

    concurrency::critical_section::scoped_lock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    DWORD dwMajor = 0L, dwMinor = 0L;
    if (FAILED(hr = SystemDetails::GetOSVersion(dwMajor, dwMinor)))
        return hr;

    if (dwMajor < 6)
    {
        if (FAILED(hr = Load(L"DBGHELP_XPDLL"s)))
            return hr;
    }
    else
    {
        if (FAILED(hr = Load()))
            return hr;
    }

    if (IsLoaded())
    {
        Get(m_ImagehlpApiVersion, "ImagehlpApiVersion");
        Get(m_MiniDumpWriteDump, "MiniDumpWriteDump");

        m_bInitialized = true;
    }
    return S_OK;
}

API_VERSION DbgHelpLibrary::GetVersion()
{
    if (FAILED(Initialize()))
    {
        API_VERSION retval;
        ZeroMemory(&retval, sizeof(API_VERSION));
        return retval;
    }
    API_VERSION* pVersion = m_ImagehlpApiVersion();
    if (pVersion != nullptr)
        return *pVersion;
    else
    {
        API_VERSION retval;
        ZeroMemory(&retval, sizeof(API_VERSION));
        return retval;
    }
}

DbgHelpLibrary::~DbgHelpLibrary() {}
