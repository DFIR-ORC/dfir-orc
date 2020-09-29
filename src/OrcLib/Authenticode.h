//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <string>
#include <map>

#include "OrcLib.h"
#include "Flags.h"

#include "CaseInsensitive.h"

#include "WinTrustExtension.h"

#include "OutputWriter.h"

#include <mscat.h>

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;
class ByteStream;

class ORCLIB_API Authenticode
{
public:
    typedef enum
    {
        AUTHENTICODE_UNKNOWN = 0,
        AUTHENTICODE_NOT_PE,
        AUTHENTICODE_SIGNED_VERIFIED,
        AUTHENTICODE_CATALOG_SIGNED_VERIFIED,
        AUTHENTICODE_SIGNED_NOT_VERIFIED,
        AUTHENTICODE_NOT_SIGNED
    } Status;

    static const FlagsDefinition AuthenticodeStatusDefs[];

    typedef struct _PE_Hashs
    {
        CBinaryBuffer md5;
        CBinaryBuffer sha1;
        CBinaryBuffer sha256;
    } PE_Hashs;

    class AuthenticodeData
    {
    public:
        bool isSigned = false;
        bool bSignatureVerifies = false;

        Authenticode::Status AuthStatus = AUTHENTICODE_UNKNOWN;

        std::vector<PCCERT_CONTEXT> Signers;
        std::vector<PCCERT_CONTEXT> SignersCAs;
        std::vector<HCERTSTORE> CertStores;
        PE_Hashs SignedHashs;
        FILETIME Timestamp{ 0 };

        AuthenticodeData() = default;
        AuthenticodeData(const AuthenticodeData& other) = default;
        AuthenticodeData(AuthenticodeData&& other)
        {
            std::swap(isSigned, other.isSigned);
            std::swap(bSignatureVerifies, other.bSignatureVerifies);
            std::swap(Signers, other.Signers);
            std::swap(SignersCAs, other.SignersCAs);
        }

        ~AuthenticodeData()
        {
            isSigned = false;
            bSignatureVerifies = false;
            for (auto signer : Signers)
            {
                CertFreeCertificateContext(signer);
            }
            for (auto CA : SignersCAs)
            {
                CertFreeCertificateContext(CA);
            }
            for (auto certStore : CertStores)
            {
                CertCloseStore(certStore, CERT_CLOSE_STORE_FORCE_FLAG);
            }
        }

        AuthenticodeData& operator=(const AuthenticodeData& other) = default;
    };

private:
    HCERTSTORE m_hMachineStore = INVALID_HANDLE_VALUE;
    HANDLE m_hContext = INVALID_HANDLE_VALUE;
    std::map<std::wstring, HANDLE, CaseInsensitive> m_StateMap;

    WinTrustExtension m_wintrust;

    HRESULT FindCatalogForHash(const CBinaryBuffer& hash, bool& isCatalogSigned, HCATINFO& hCatalog);

    HRESULT EvaluateCheck(LONG lStatus, AuthenticodeData& data);

    HRESULT VerifyEmbeddedSignature(LPCWSTR szFileName, HANDLE hFile, AuthenticodeData& data);
    HRESULT VerifySignatureWithCatalogs(LPCWSTR szFileName, const CBinaryBuffer& hash, HCATINFO& hCatalog, AuthenticodeData& data);

    HRESULT ExtractSignatureSize(const CBinaryBuffer& signature, DWORD& cbSize);
    HRESULT ExtractSignatureHash(const CBinaryBuffer& signature, AuthenticodeData& data);
    HRESULT ExtractSignatureTimeStamp(const CBinaryBuffer& signature, AuthenticodeData& data);

    HRESULT ExtractNestedSignature(
        HCRYPTMSG hMsg,
        DWORD dwSignerIndex,
        std::vector<PCCERT_CONTEXT>& pSigner,
        std::vector<HCERTSTORE>& certStores);

    HRESULT ExtractSignatureSigners(
        const CBinaryBuffer& signature,
        const FILETIME& timestamp,
        std::vector<PCCERT_CONTEXT>& pSigner,
        std::vector<PCCERT_CONTEXT>& pCA,
        std::vector<HCERTSTORE>& certStores);

    HRESULT ExtractCatalogSigners(
        LPCWSTR szCatalogFile,
        std::vector<PCCERT_CONTEXT>& pSigners,
        std::vector<PCCERT_CONTEXT>& pCAs,
        std::vector<HCERTSTORE>& certStores);

public:
    Authenticode();

    static DWORD ExpectedHashSize();

    // Catalog based verifications
    HRESULT Verify(LPCWSTR szFileName, AuthenticodeData& data);
    HRESULT Verify(LPCWSTR szFileName, const std::shared_ptr<ByteStream>& pStream, AuthenticodeData& data);
    HRESULT VerifyAnySignatureWithCatalogs(LPCWSTR szFileName, const PE_Hashs& hashs, AuthenticodeData& data);

    // Security directory verification
    HRESULT Verify(LPCWSTR szFileName, const CBinaryBuffer& secdir, const PE_Hashs& hashs, AuthenticodeData& data);
    HRESULT SignatureSize(LPCWSTR szFileName, const CBinaryBuffer& secdir, DWORD& cbSize);

    HRESULT CloseCatalogState();

    ~Authenticode();
};

}  // namespace Orc
#pragma managed(pop)
