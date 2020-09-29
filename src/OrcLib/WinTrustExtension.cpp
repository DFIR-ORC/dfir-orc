//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "WinTrustExtension.h"

using namespace Orc;

HRESULT WinTrustExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Try(m_CryptCATAdminAcquireContext2, "CryptCATAdminAcquireContext2");
        Get(m_CryptCATAdminAcquireContext, "CryptCATAdminAcquireContext");

        Try(m_CryptCATAdminCalcHashFromFileHandle2, "CryptCATAdminCalcHashFromFileHandle2");
        Get(m_CryptCATAdminCalcHashFromFileHandle, "CryptCATAdminCalcHashFromFileHandle");

        Get(m_CryptCATAdminEnumCatalogFromHash, "CryptCATAdminEnumCatalogFromHash");

        Get(m_CryptCATCatalogInfoFromContext, "CryptCATCatalogInfoFromContext");

        Get(m_CryptCATAdminReleaseCatalogContext, "CryptCATAdminReleaseCatalogContext");

        Get(m_WinVerifyTrust, "WinVerifyTrust");

        Get(m_CryptCATAdminReleaseContext, "CryptCATAdminReleaseContext");

        m_bInitialized = true;
    }
    return S_OK;
}

HRESULT Orc::WinTrustExtension::CryptCATAdminAcquireContext(HCATADMIN* phCatAdmin)
{
    if (FAILED(Initialize()))
        return E_FAIL;

    if (m_CryptCATAdminAcquireContext2 != nullptr)
    {
        CERT_STRONG_SIGN_PARA params;
        params.cbSize = sizeof(CERT_STRONG_SIGN_PARA);

        params.dwInfoChoice = CERT_STRONG_SIGN_OID_INFO_CHOICE;
        params.pszOID = szOID_CERT_STRONG_KEY_OS_CURRENT;
		if (!m_CryptCATAdminAcquireContext2(phCatAdmin, NULL, NULL, &params, 0L))
			return HRESULT_FROM_WIN32(GetLastError());
		else
			return S_OK;
    }
    else
        return m_CryptCATAdminAcquireContext(phCatAdmin, NULL, 0L);
}

HRESULT __stdcall Orc::WinTrustExtension::CryptCATAdminCalcHashFromFileHandle(
    HCATADMIN hCatAdmin,
    HANDLE hFile,
    DWORD* pcbHash,
    BYTE* pbHash)
{
    if (FAILED(Initialize()))
        return E_FAIL;

    if (m_CryptCATAdminCalcHashFromFileHandle2 != nullptr)
    {
        return m_CryptCATAdminCalcHashFromFileHandle2(hCatAdmin, hFile, pcbHash, pbHash, 0L);
    }
    else
    {
        return m_CryptCATAdminCalcHashFromFileHandle(hFile, pcbHash, pbHash, 0L);
    }
}

LONG __stdcall Orc::WinTrustExtension::WinVerifyTrust(HWND hWnd, GUID* pgActionID, LPVOID pWVTData)
{
    if (FAILED(Initialize()))
        return 0L;

    __try
    {
        return m_WinVerifyTrust(hWnd, pgActionID, pWVTData);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        spdlog::error("Exception raised in WinVerifyTrust");
        return 0L;
    }
}

WinTrustExtension::~WinTrustExtension() {}
