//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "CryptoUtilities.h"

using namespace Orc;

HRESULT CryptoUtilities::AcquireContext(_Out_ HCRYPTPROV& hCryptProv)
{
    HRESULT hr = E_FAIL;
    LPCWSTR szProviders[] = {MS_ENH_RSA_AES_PROV, MS_ENH_RSA_AES_PROV_XP_W, MS_ENHANCED_PROV, NULL};

    LPCWSTR* pszProvider = szProviders;
    while (hCryptProv == NULL && pszProvider != NULL)
    {
        if (!CryptAcquireContext(&hCryptProv, NULL, *pszProvider, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            spdlog::debug(L"Failed to initialize provider: '{}' (code: {:#x})", *pszProvider, hr);
        }
        else
            break;
        pszProvider++;
    }
    if (hCryptProv == NULL)
    {
        spdlog::error("Failed to initialize providers");
        return hr;
    }
    else if (pszProvider && *pszProvider)
    {
        spdlog::debug(L"Initalized provider: '{}'", *pszProvider);
    }
    return S_OK;
}
