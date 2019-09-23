//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "ExtensionLibrary.h"

#include <mscat.h>
#include <WinCrypt.h>

#pragma managed(push, off)

namespace Orc {
//+=========================================================================
//  Certificate Strong Signature Defines and Data Structures
//==========================================================================
#ifndef CERT_STRONG_SIGN_ECDSA_ALGORITHM

struct CERT_STRONG_SIGN_SERIALIZED_INFO
{
    DWORD dwFlags;
    LPWSTR pwszCNGSignHashAlgids;
    LPWSTR pwszCNGPubKeyMinBitLengths;  // Optional
};
using PCERT_STRONG_SIGN_SERIALIZED_INFO = CERT_STRONG_SIGN_SERIALIZED_INFO *;

struct CERT_STRONG_SIGN_PARA
{
    DWORD cbSize;

    DWORD dwInfoChoice;
    union
    {
        void* pvInfo;

        // CERT_STRONG_SIGN_SERIALIZED_INFO_CHOICE
        PCERT_STRONG_SIGN_SERIALIZED_INFO pSerializedInfo;

        // CERT_STRONG_SIGN_OID_INFO_CHOICE
        LPSTR pszOID;

    } DUMMYUNIONNAME;
};
using PCERT_STRONG_SIGN_PARA = CERT_STRONG_SIGN_PARA*;
using PCCERT_STRONG_SIGN_PARA = const CERT_STRONG_SIGN_PARA*;

#    define CERT_STRONG_SIGN_SERIALIZED_INFO_CHOICE 1
#    define CERT_STRONG_SIGN_OID_INFO_CHOICE 2

#    define szOID_CERT_STRONG_KEY_OS_1 "1.3.6.1.4.1.311.72.2.1"
#    define szOID_CERT_STRONG_KEY_OS_CURRENT szOID_CERT_STRONG_KEY_OS_1

#endif

using namespace std::string_literals;

class WinTrustExtension : public ExtensionLibrary
{

public:
    WinTrustExtension(logger pLog)
        : ExtensionLibrary(std::move(pLog), L"wintrust"s, L"wintrust.dll"s, L"wintrust.dll"s) {};
    ~WinTrustExtension();

    STDMETHOD(Initialize)();

    template <typename... Args>
    auto CryptCATAdminAcquireContext(Args&&... args)
    {
        if (FAILED(Initialize()))
            return E_FAIL;
        return m_CryptCATAdminAcquireContext(std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CryptCATAdminAcquireContext2(Args&&... args)
    {
        if (FAILED(Initialize()))
            return E_FAIL;
        return m_CryptCATAdminAcquireContext2(std::forward<Args>(args)...);
    }

    HRESULT CryptCATAdminAcquireContext(_Out_ HCATADMIN* phCatAdmin);

    template <typename... Args>
    auto CryptCATAdminCalcHashFromFileHandle(Args&&... args)
    {
        if (FAILED(Initialize()))
            return FALSE;
        return m_CryptCATAdminCalcHashFromFileHandle(std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CryptCATAdminCalcHashFromFileHandle2(Args&&... args)
    {
        if (FAILED(Initialize()))
            return E_FAIL;
        return m_CryptCATAdminCalcHashFromFileHandle2(std::forward<Args>(args)...);
    }

    HRESULT WINAPI CryptCATAdminCalcHashFromFileHandle(
        _In_ HCATADMIN hCatAdmin,
        _In_ HANDLE hFile,
        _Inout_ DWORD* pcbHash,
        _In_ BYTE* pbHash);

    template <typename... Args>
    auto CryptCATCatalogInfoFromContext(Args&&... args)
    {
        if (FAILED(Initialize()))
            return FALSE;
        return m_CryptCATCatalogInfoFromContext(std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CryptCATAdminEnumCatalogFromHash(Args&&... args)
    {
        if (FAILED(Initialize()))
            return (HCATINFO)NULL;
        return m_CryptCATAdminEnumCatalogFromHash(std::forward<Args>(args)...);
    }

    LONG WINAPI WinVerifyTrust(_In_ HWND hWnd, _In_ GUID* pgActionID, _In_ LPVOID pWVTData);

    template <typename... Args>
    auto CryptCATAdminReleaseCatalogContext(Args&&... args)
    {
        if (FAILED(Initialize()))
            return FALSE;
        return m_CryptCATAdminReleaseCatalogContext(std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CryptCATAdminReleaseContext(Args&&... args)
    {
        if (FAILED(Initialize()))
            return FALSE;
        return m_CryptCATAdminReleaseContext(std::forward<Args>(args)...);
    }

private:
    BOOL(WINAPI* m_CryptCATAdminAcquireContext)
    (_Out_ HCATADMIN* phCatAdmin, _In_ const GUID* pgSubsystem, _In_ DWORD dwFlags) = nullptr;
    BOOL(WINAPI* m_CryptCATAdminAcquireContext2)
    (_Out_ HCATADMIN* phCatAdmin,
     _In_opt_ const GUID* pgSubsystem,
     _In_opt_ PCWSTR pwszHashAlgorithm,
     _In_opt_ PCCERT_STRONG_SIGN_PARA pStrongHashPolicy,
     _Reserved_ DWORD dwFlags) = nullptr;

    BOOL(WINAPI* m_CryptCATAdminCalcHashFromFileHandle)
    (_In_ HANDLE hFile, _Inout_ DWORD* pcbHash, _In_ BYTE* pbHash, _In_ DWORD dwFlags) = nullptr;
    BOOL(WINAPI* m_CryptCATAdminCalcHashFromFileHandle2)
    (_In_ HCATADMIN hCatAdmin,
     _In_ HANDLE hFile,
     _Inout_ DWORD* pcbHash,
     _Out_writes_bytes_to_opt_(*pcbHash, *pcbHash) BYTE* pbHash,
     _Reserved_ DWORD dwFlags) = nullptr;

    BOOL(WINAPI* m_CryptCATCatalogInfoFromContext)
    (_In_ HCATINFO hCatInfo, _Inout_ CATALOG_INFO* psCatInfo, _In_ DWORD dwFlags) = nullptr;

    HCATINFO(WINAPI* m_CryptCATAdminEnumCatalogFromHash)
    (_In_ HCATADMIN hCatAdmin, _In_ BYTE* pbHash, _In_ DWORD cbHash, _In_ DWORD dwFlags, _In_ HCATINFO* phPrevCatInfo) =
        nullptr;

    BOOL(WINAPI* m_CryptCATAdminReleaseCatalogContext)
    (_In_ HCATADMIN hCatAdmin, _In_ HCATINFO hCatInfo, _In_ DWORD dwFlags) = nullptr;

    BOOL(WINAPI* m_CryptCATAdminReleaseContext)(_In_ HCATADMIN hCatAdmin, _In_ DWORD dwFlags) = nullptr;

    LONG(WINAPI* m_WinVerifyTrust)(_In_ HWND hWnd, _In_ GUID* pgActionID, _In_ LPVOID pWVTData) = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
