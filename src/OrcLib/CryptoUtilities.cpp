//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "CryptoUtilities.h"

#include "LogFileWriter.h"

using namespace Orc;

HRESULT CryptoUtilities::AcquireContext(_In_ const logger& pLog, _Out_ HCRYPTPROV& hCryptProv)
{
    HRESULT hr = E_FAIL;
    LPCWSTR szProviders[] = {MS_ENH_RSA_AES_PROV, MS_ENH_RSA_AES_PROV_XP_W, MS_ENHANCED_PROV, NULL};

    LPCWSTR* pszProvider = szProviders;
    while (hCryptProv == NULL && pszProvider != NULL)
    {
        if (!CryptAcquireContext(&hCryptProv, NULL, *pszProvider, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            log::Verbose(
                pLog,
                L"Failed to initialize provider: %s (hr=0x%lx)\r\n",
                *pszProvider,
                hr = HRESULT_FROM_WIN32(GetLastError()));
        }
        else
            break;
        pszProvider++;
    }
    if (hCryptProv == NULL)
    {
        log::Error(pLog, hr, L"Failed to initialize providers\r\n");
        return hr;
    }
    else if (pszProvider && *pszProvider)
    {
        log::Verbose(pLog, L"Initalized provider: %s\r\n", *pszProvider);
    }
    return S_OK;
}
