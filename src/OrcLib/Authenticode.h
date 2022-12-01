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
#include "BinaryBuffer.h"

#include <mscat.h>
#include <algorithm>

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;
class ByteStream;

class AuthenticodeCache
{
public:
    struct SignersInfo
    {
        using Thumbprint = std::vector<uint8_t>;

        std::wstring_view catalogPath;
        std::vector<std::wstring> names;
        std::vector<Thumbprint> thumbprints;
        std::vector<std::wstring> certificateAuthoritiesName;
        std::vector<Thumbprint> certificateAuthoritiesThumbprint;
    };

    using CatalogPath = std::wstring_view;
    using SignersCache = std::unordered_map<CatalogPath, std::shared_ptr<SignersInfo>>;

    std::shared_ptr<SignersInfo> Find(std::wstring_view catalogPath)
    {
        auto it = m_signers.find(catalogPath);
        if (it != std::end(m_signers))
        {
            return it->second;
        }

        return nullptr;
    }

    const std::shared_ptr<SignersInfo>& Update(std::wstring_view catalogPath, std::shared_ptr<SignersInfo> signersInfo)
    {
        // Required workaround for lifetime until until heterogeneous lookup in unordered map are available (using
        // std::string_view key for a std::string key map)
        m_catalogsPath.emplace_back(catalogPath);

        return m_signers[m_catalogsPath.back()] = std::move(signersInfo);
    }

    SignersCache& Signers() { return m_signers; }
    const SignersCache& Signers() const { return m_signers; }

private:
    std::vector<std::wstring> m_catalogsPath;
    SignersCache m_signers;
};

class Authenticode
{
public:
    using Thumbprint = AuthenticodeCache::SignersInfo::Thumbprint;

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
        Authenticode::Status AuthStatus = AUTHENTICODE_UNKNOWN;

        AuthenticodeData() {}
        AuthenticodeData(std::shared_ptr<AuthenticodeCache> cache)
            : m_authenticodeCache(std::move(cache)) {};
        AuthenticodeData(const AuthenticodeData& other) = default;
        AuthenticodeData(AuthenticodeData&& other)
        {
            std::swap(isSigned, other.isSigned);
            std::swap(bSignatureVerifies, other.bSignatureVerifies);
            std::swap(m_signers, other.m_signers);
            std::swap(m_signersName, other.m_signersName);
            std::swap(m_signersCertificateAuthoritiesThumbprint, other.m_signersCertificateAuthoritiesThumbprint);
            std::swap(m_signersCertificateAuthorities, other.m_signersCertificateAuthorities);
            std::swap(m_signersCertificateAuthoritiesName, other.m_signersCertificateAuthoritiesName);
            std::swap(m_signersCertificateAuthoritiesThumbprint, other.m_signersCertificateAuthoritiesThumbprint);
            std::swap(m_signersInfo, other.m_signersInfo);
        }

        ~AuthenticodeData()
        {
            isSigned = false;
            bSignatureVerifies = false;
            for (auto signer : m_signers)
            {
                CertFreeCertificateContext(signer);
            }
            for (auto CA : m_signersCertificateAuthorities)
            {
                CertFreeCertificateContext(CA);
            }
            for (auto certStore : CertStores)
            {
                CertCloseStore(certStore, CERT_CLOSE_STORE_FORCE_FLAG);
            }
        }

        const std::vector<std::wstring>& SignersName();
        const std::vector<Authenticode::Thumbprint>& SignersThumbprint();

        const std::vector<std::wstring>& SignersCertificateAuthoritiesName();
        const std::vector<Authenticode::Thumbprint>& SignersCertificateAuthoritiesThumbprint();

        const std::shared_ptr<Orc::AuthenticodeCache>& AuthenticodeCache() const { return m_authenticodeCache; }
        std::shared_ptr<Orc::AuthenticodeCache>& AuthenticodeCache() { return m_authenticodeCache; }

        AuthenticodeData& operator=(const AuthenticodeData& other) = default;

        void SetSignerInfo(std::shared_ptr<AuthenticodeCache::SignersInfo> signersInfo) { m_signersInfo = signersInfo; }
        const std::shared_ptr<AuthenticodeCache::SignersInfo>& SignersInfo() const { return m_signersInfo; }

        // private:
        std::vector<PCCERT_CONTEXT>& Signers() { return m_signers; }
        const std::vector<PCCERT_CONTEXT>& Signers() const { return m_signers; }

        std::vector<PCCERT_CONTEXT>& SignersCAs() { return m_signersCertificateAuthorities; }
        const std::vector<PCCERT_CONTEXT>& SignersCAs() const { return m_signersCertificateAuthorities; }

    public:
        std::vector<HCERTSTORE> CertStores;
        bool isSigned = false;
        bool bSignatureVerifies = false;
        PE_Hashs SignedHashs;
        FILETIME Timestamp {0};

    private:
        mutable std::shared_ptr<Orc::AuthenticodeCache> m_authenticodeCache;
        std::shared_ptr<Orc::AuthenticodeCache::SignersInfo> m_signersInfo;

        std::optional<std::vector<std::wstring>> m_signersName;
        std::optional<std::vector<Thumbprint>> m_signersThumbprint;
        std::optional<std::vector<std::wstring>> m_signersCertificateAuthoritiesName;
        std::optional<std::vector<Thumbprint>> m_signersCertificateAuthoritiesThumbprint;

        std::vector<PCCERT_CONTEXT> m_signers;
        std::vector<PCCERT_CONTEXT> m_signersCertificateAuthorities;
    };

public:
    Authenticode();
    ~Authenticode();

    static DWORD ExpectedHashSize();

    static HRESULT
    ExtractCatalogSigners(std::wstring_view catalogPath, std::string_view catalogData, AuthenticodeData& data);

    static Orc::Result<void> VerifySignatureWithCatalogHint(
        std::string_view catalogHint,
        const Orc::Authenticode::PE_Hashs& peHashes,
        Authenticode::AuthenticodeData& data);

    // Catalog based verifications
    HRESULT Verify(LPCWSTR szFileName, AuthenticodeData& data);
    HRESULT Verify(LPCWSTR szFileName, const std::shared_ptr<ByteStream>& pStream, AuthenticodeData& data);
    HRESULT VerifyAnySignatureWithCatalogs(LPCWSTR szFileName, const PE_Hashs& hashs, AuthenticodeData& data);

    // Security directory verification
    HRESULT Verify(LPCWSTR szFileName, const CBinaryBuffer& secdir, const PE_Hashs& hashs, AuthenticodeData& data);
    HRESULT SignatureSize(LPCWSTR szFileName, const CBinaryBuffer& secdir, DWORD& cbSize);

    std::shared_ptr<AuthenticodeCache>& Cache() { return m_authenticodeCache; }
    void SetCache(std::shared_ptr<AuthenticodeCache> cache) { m_authenticodeCache = std::move(cache); }

private:
    static HRESULT ExtractCatalogSigners(
        std::string_view catalog,
        std::vector<PCCERT_CONTEXT>& pSigners,
        std::vector<PCCERT_CONTEXT>& pCAs,
        std::vector<HCERTSTORE>& certStores);

    HRESULT FindCatalogForHash(const CBinaryBuffer& hash, bool& isCatalogSigned, HCATINFO& hCatalog);

    HRESULT EvaluateCheck(LONG lStatus, AuthenticodeData& data);

    HRESULT VerifyEmbeddedSignature(LPCWSTR szFileName, HANDLE hFile, AuthenticodeData& data);

    HRESULT VerifySignatureWithCatalogs(
        LPCWSTR szFileName,
        const CBinaryBuffer& hash,
        HCATINFO& hCatalog,
        AuthenticodeData& data);

    HRESULT ExtractSignatureSize(const CBinaryBuffer& signature, DWORD& cbSize);
    HRESULT ExtractSignatureHash(const CBinaryBuffer& signature, AuthenticodeData& data);
    HRESULT ExtractSignatureTimeStamp(const CBinaryBuffer& signature, AuthenticodeData& data);

    static HRESULT ExtractNestedSignature(
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

private:
    HCERTSTORE m_hMachineStore = INVALID_HANDLE_VALUE;
    HANDLE m_hContext = INVALID_HANDLE_VALUE;
    WinTrustExtension m_wintrust;
    std::shared_ptr<AuthenticodeCache> m_authenticodeCache;
};

}  // namespace Orc
#pragma managed(pop)
