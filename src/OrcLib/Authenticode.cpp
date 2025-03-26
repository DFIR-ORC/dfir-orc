//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "Authenticode.h"

#include "Flags.h"
#include "ByteStream.h"
#include "CacheStream.h"
#include "MemoryStream.h"
#include "CryptoHashStream.h"
#include "FileStream.h"

#include "SystemDetails.h"

#include "WinTrustExtension.h"

#include <mscat.h>

#include <array>

#include <sstream>

#include <boost/scope_exit.hpp>

#include "FileFormat/PeParser.h"
#include "Utils/Guard.h"
#include "Utils/WinApi.h"

using namespace Orc;

namespace {

template <typename ContainerT>
Orc::Result<void> MapFile(const std::wstring& path, ContainerT& output)
{
    using namespace Orc;

    Guard::FileHandle hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (!hFile.IsValid())
    {
        return LastWin32Error();
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(*hFile, &fileSize))
    {
        return LastWin32Error();
    }

    try
    {
        output.resize(fileSize.QuadPart);
    }
    catch (...)
    {
        return std::errc::not_enough_memory;
    }

    DWORD dwBytesRead;
    if (!ReadFile(*hFile, output.data(), output.size(), &dwBytesRead, NULL))
    {
        return LastWin32Error();
    }

    return Success<void>();
}

Orc::Result<std::wstring> ParseCatalogHintFilename(std::string_view catalogHint)
{
    using namespace Orc;

    struct CatalogHintAttribute
    {
        uint16_t unknownShort;
        uint16_t nameLength;
    };

    if (sizeof(CatalogHintAttribute) > catalogHint.size())
    {
        Log::Debug("Invalid EA $CI.CATALOGHINT item size");
        return std::errc::bad_message;
    }

    auto hint = reinterpret_cast<const CatalogHintAttribute*>(catalogHint.data());

    if (hint->nameLength + sizeof(CatalogHintAttribute) > catalogHint.size())
    {
        Log::Debug("Invalid EA $CI.CATALOGHINT size");
        return std::errc::bad_message;
    }

    std::error_code ec;
    std::string_view utf8(catalogHint.data() + sizeof(CatalogHintAttribute), hint->nameLength);
    const auto name = ToUtf16(utf8, ec);
    if (ec)
    {
        return ec;
    }

    return name;
}

Orc::Result<void> CheckCatalogSignature(std::string_view catalog)
{
    using namespace Orc;

    CRYPT_VERIFY_MESSAGE_PARA verifyParameters = {};
    verifyParameters.cbSize = sizeof(verifyParameters);
    verifyParameters.dwMsgAndCertEncodingType = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;
    verifyParameters.hCryptProv = NULL;
    verifyParameters.pfnGetSignerCertificate = NULL;
    verifyParameters.pvGetArg = NULL;

    // If 'signer' is not provided the catalog signature will not be checked
    PCCERT_CONTEXT signer = NULL;

    DWORD decodedMessageLength = 0;
    BOOL result = CryptVerifyMessageSignature(
        &verifyParameters,
        0,
        reinterpret_cast<unsigned char*>(const_cast<char*>(catalog.data())),
        catalog.size(),
        NULL,
        &decodedMessageLength,
        &signer);
    if (!result)
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed CryptVerifyMessageSignature [{}]", ec);
        return ec;
    }

    if (signer)
    {
        if (!CertFreeCertificateContext(signer))
        {
            Log::Debug(L"Failed CertFreeCertificateContext [{}]", LastWin32Error());
        }
    }

    return Orc::Success<void>();
}

bool FindPeHash(std::string_view buffer, const Orc::Authenticode::PE_Hashs& peHashes)
{
    std::array<std::string_view, 2> hashes = {peHashes.sha1, peHashes.sha256};

    for (const auto& hash : hashes)
    {
        if (hash.size() == 0)
        {
            continue;
        }

        if (buffer.find(hash) != std::string_view::npos)
        {
            return true;
        }
    }

    return false;
}

Authenticode::PE_Hashs To_PE_Hashs(const PeParser::PeHash& hashes)
{
    Authenticode::PE_Hashs h;

    if (hashes.md5.has_value())
    {
        h.md5.SetCount(hashes.md5->size());
        std::copy(std::cbegin(*hashes.md5), std::cend(*hashes.md5), h.md5.GetData());
    }

    if (hashes.sha1.has_value())
    {
        h.sha1.SetCount(hashes.sha1->size());
        std::copy(std::cbegin(*hashes.sha1), std::cend(*hashes.sha1), h.sha1.GetData());
    }

    if (hashes.sha256.has_value())
    {
        h.sha256.SetCount(hashes.sha256->size());
        std::copy(std::cbegin(*hashes.sha256), std::cend(*hashes.sha256), h.sha256.GetData());
    }

    return h;
}

void GetSignersName(const std::vector<PCCERT_CONTEXT>& signers, std::vector<std::wstring>& names)
{
    for (const auto& signer : signers)
    {
        std::wstring name;
        const auto requiredLength = CertGetNameStringW(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0L, NULL, NULL, 0);
        name.resize(requiredLength);

        CertGetNameStringW(signer, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0L, NULL, name.data(), name.size());
        name.resize(requiredLength - 1);  // remove trailing '\0'

        names.push_back(std::move(name));
    }
}

void GetSignersCertificateAuthoritiesName(
    const std::vector<PCCERT_CONTEXT>& certificateAuthorities,
    std::vector<std::wstring>& names)
{
    for (const auto& ca : certificateAuthorities)
    {
        std::wstring name;
        const auto requiredLength = CertGetNameStringW(ca, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0L, NULL, NULL, 0);
        name.resize(requiredLength);

        CertGetNameStringW(ca, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0L, NULL, name.data(), name.size());
        name.resize(requiredLength - 1);  // remove trailing '\0'

        names.push_back(std::move(name));
    }
}

HRESULT
GetSignersThumbprint(const std::vector<PCCERT_CONTEXT>& signers, std::vector<Authenticode::Thumbprint>& thumbprints)
{
    for (const auto& signer : signers)
    {
        std::vector<uint8_t> thumbprint;
        thumbprint.resize(BYTES_IN_SHA256_HASH);

        DWORD thumbprintSize = thumbprint.size();
        if (!CertGetCertificateContextProperty(signer, CERT_HASH_PROP_ID, thumbprint.data(), &thumbprintSize))
        {
            auto lastError = GetLastError();
            Log::Debug("Failed to extract signer thumbprint [{}]", Win32Error(lastError));
            return HRESULT_FROM_WIN32(lastError);
        }

        thumbprint.resize(thumbprintSize);
        thumbprints.push_back(std::move(thumbprint));
    }

    return S_OK;
}

HRESULT GetSignersCertificateAuthoritiesThumbprint(
    const std::vector<PCCERT_CONTEXT>& signers,
    std::vector<Authenticode::Thumbprint>& thumbprints)
{
    for (const auto& signer : signers)
    {
        std::vector<uint8_t> thumbprint;
        thumbprint.resize(BYTES_IN_SHA256_HASH);

        DWORD thumbprintSize = thumbprint.size();
        if (!CertGetCertificateContextProperty(signer, CERT_HASH_PROP_ID, thumbprint.data(), &thumbprintSize))
        {
            auto lastError = GetLastError();
            Log::Debug("Failed to extract signers certificate authorities thumbprint [{}]", Win32Error(lastError));
            return HRESULT_FROM_WIN32(lastError);
        }

        thumbprint.resize(thumbprintSize);
        thumbprints.push_back(std::move(thumbprint));
    }

    return S_OK;
}

const std::shared_ptr<AuthenticodeCache::SignersInfo>& UpdateSignersCache(
    AuthenticodeCache& cache,
    std::wstring_view catalogPath,
    std::vector<PCCERT_CONTEXT>& signers,
    std::vector<PCCERT_CONTEXT>& signersCertificateAuthorities)
{
    auto info = std::make_shared<AuthenticodeCache::SignersInfo>();
    info->catalogPath = catalogPath;

    GetSignersName(signers, info->names);
    GetSignersCertificateAuthoritiesName(signersCertificateAuthorities, info->certificateAuthoritiesName);

    HRESULT hr = GetSignersThumbprint(signers, info->thumbprints);
    if (FAILED(hr))
    {
        Log::Error("Failed to extract signers thumprint [{}]", SystemError(hr));
    }

    hr = GetSignersCertificateAuthoritiesThumbprint(
        signersCertificateAuthorities, info->certificateAuthoritiesThumbprint);
    if (FAILED(hr))
    {
        Log::Error("Failed to extract signers certificate authorities thumprint [{}]", SystemError(hr));
    }

    auto signersInfo = std::make_shared<AuthenticodeCache::SignersInfo>();

    return cache.Update(catalogPath, std::move(info));
}

}  // namespace

static GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;

const FlagsDefinition Authenticode::AuthenticodeStatusDefs[] = {
    {AUTHENTICODE_UNKNOWN, L"Unknown", L"This file status is unknown"},
    {AUTHENTICODE_NOT_PE, L"NotPE", L"This is not a PE"},
    {AUTHENTICODE_SIGNED_VERIFIED, L"SignedVerified", L"This PE is signed and signature verifies"},
    {AUTHENTICODE_CATALOG_SIGNED_VERIFIED, L"CatalogSignedVerified", L"This PE's hash is catalog signed"},
    {AUTHENTICODE_SIGNED_NOT_VERIFIED, L"SignedNotVerified", L"This PE's signature does not verify"},
    {AUTHENTICODE_NOT_SIGNED, L"NotSigned", L"This PE is not signed"},
    {(DWORD)-1, NULL, NULL}};

// TODO: failure should be handleable by caller
Authenticode::Authenticode()
    : m_wintrust()
{
    HRESULT hr = E_FAIL;

    if (!m_wintrust.IsLoaded())
    {
        if (FAILED(hr = m_wintrust.Load()))
        {
            Log::Error("Failed to load WinTrust [{}]", SystemError(hr));
            return;
        }
    }

    if (FAILED(hr = m_wintrust.CryptCATAdminAcquireContext(&m_hContext)))
    {
        Log::Error("Failed to CryptCATAdminAcquireContext [{}]", SystemError(hr));
        return;
    }

    m_hMachineStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM,
        PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
        NULL,
        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG,
        L"MY");
    if (m_hMachineStore == NULL)
    {
        Log::Error("Failed CertOpenStore [{}]", LastWin32Error());
        return;
    }
}

DWORD Authenticode::ExpectedHashSize()
{
    const auto wintrust = ExtensionLibrary::GetLibrary<WinTrustExtension>();

    WCHAR szPath[ORC_MAX_PATH];

    if (!ExpandEnvironmentStrings(L"%windir%\\System32\\kernel32.dll", szPath, ORC_MAX_PATH))
    {
        return (DWORD)-1;
    }

    HANDLE hFile = CreateFile(
        szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0L, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return (DWORD)-1;
    BOOST_SCOPE_EXIT(hFile)
    {
        CloseHandle(hFile);
    }
    BOOST_SCOPE_EXIT_END;

    HCATADMIN hContext = NULL;

    if (auto hr = wintrust->CryptCATAdminAcquireContext(&hContext); FAILED(hr))
    {
        return (DWORD)-1;
    }
    BOOST_SCOPE_EXIT(hContext)
    {
        CryptCATAdminReleaseContext(hContext, 0);
    }
    BOOST_SCOPE_EXIT_END;

    DWORD cbHash = 0L;
    if (auto hr = wintrust->CryptCATAdminCalcHashFromFileHandle(hFile, &cbHash, nullptr, 0); FAILED(hr))
    {
        return (DWORD)-1;
    }

    return cbHash;
}

// this code is from http://msdn.microsoft.com/en-us/library/aa382384(VS.85).aspx
// with additions on catalog programming from http://forum.sysinternals.com/topic19247.html

HRESULT Authenticode::FindCatalogForHash(const CBinaryBuffer& hash, bool& isCatalogSigned, HCATINFO& hCatalog)
{
    // Convert the hash to a string.
    // each byte of the hash gets represented by 2 wide chars
    isCatalogSigned = false;
    hCatalog = INVALID_HANDLE_VALUE;

    CATALOG_INFO InfoStruct;
    // Zero our structures.
    memset(&InfoStruct, 0, sizeof(CATALOG_INFO));
    InfoStruct.cbStruct = sizeof(CATALOG_INFO);

    // Get catalog for our context.
    HCATINFO CatalogContext =
        m_wintrust.CryptCATAdminEnumCatalogFromHash(m_hContext, hash.GetData(), (DWORD)hash.GetCount(), 0, nullptr);
    if (CatalogContext)
    {
        // If we couldn't get information
        if (!m_wintrust.CryptCATCatalogInfoFromContext(CatalogContext, &InfoStruct, 0))
        {
            BOOL ret = m_wintrust.CryptCATAdminReleaseCatalogContext(m_hContext, CatalogContext, 0);
            if (ret == FALSE)
            {
                Log::Error("Failed CryptCATAdminReleaseCatalogContext [{}]", LastWin32Error());
            }

            CatalogContext = NULL;
        }
        else
        {
            isCatalogSigned = true;
            hCatalog = CatalogContext;
        }
    }

    return S_OK;
}

HRESULT Authenticode::VerifyEmbeddedSignature(LPCWSTR szFileName, HANDLE hFile, AuthenticodeData& data)
{
    // Initialize the WINTRUST_FILE_INFO structure.
    WINTRUST_FILE_INFO FileData;
    memset(&FileData, 0, sizeof(FileData));
    FileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    FileData.pcwszFilePath = szFileName;
    FileData.hFile = hFile;
    FileData.pgKnownSubject = NULL;

    WINTRUST_DATA WintrustStructure;
    memset(&WintrustStructure, 0, sizeof(WintrustStructure));
    WintrustStructure.cbStruct = sizeof(WINTRUST_DATA);
    WintrustStructure.dwUIChoice = WTD_UI_NONE;
    WintrustStructure.fdwRevocationChecks = WTD_REVOKE_NONE;
    WintrustStructure.dwUnionChoice = WTD_CHOICE_FILE;
    WintrustStructure.pFile = &FileData;
    WintrustStructure.dwStateAction = WTD_STATEACTION_IGNORE;
    WintrustStructure.dwProvFlags = WTD_SAFER_FLAG;

    LONG lStatus = m_wintrust.WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &WVTPolicyGUID, &WintrustStructure);

    return EvaluateCheck(lStatus, data);
}

HRESULT
Authenticode::VerifySignatureWithCatalogs(
    LPCWSTR szFileName,
    const CBinaryBuffer& hash,
    HCATINFO& hCatalog,
    AuthenticodeData& data)
{
    DBG_UNREFERENCED_PARAMETER(szFileName);
    CATALOG_INFO InfoStruct;
    // Zero our structures.
    memset(&InfoStruct, 0, sizeof(CATALOG_INFO));
    InfoStruct.cbStruct = sizeof(CATALOG_INFO);

    // If we couldn't get information
    if (!m_wintrust.CryptCATCatalogInfoFromContext(hCatalog, &InfoStruct, 0))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring MemberTag = hash.ToHex();

    Log::Debug(L"The file is associated with catalog: '{}'", InfoStruct.wszCatalogFile);

    // Fill in catalog info structure.
    WINTRUST_CATALOG_INFO WintrustCatalogStructure;
    ZeroMemory(&WintrustCatalogStructure, sizeof(WINTRUST_CATALOG_INFO));
    WintrustCatalogStructure.cbStruct = sizeof(WINTRUST_CATALOG_INFO);
    WintrustCatalogStructure.pcwszCatalogFilePath = InfoStruct.wszCatalogFile;
    WintrustCatalogStructure.cbCalculatedFileHash = (DWORD)hash.GetCount();
    WintrustCatalogStructure.pbCalculatedFileHash = hash.GetData();
    WintrustCatalogStructure.pcwszMemberTag = MemberTag.c_str();
    WintrustCatalogStructure.pcwszMemberFilePath = nullptr;

    // If we get here, we have catalog info!  Verify it.
    WINTRUST_DATA WintrustStructure;
    ZeroMemory(&WintrustStructure, sizeof(WintrustStructure));
    WintrustStructure.cbStruct = sizeof(WINTRUST_DATA);
    WintrustStructure.dwUIChoice = WTD_UI_NONE;
    WintrustStructure.fdwRevocationChecks = WTD_REVOKE_NONE;
    WintrustStructure.dwUnionChoice = WTD_CHOICE_CATALOG;
    WintrustStructure.pCatalog = &WintrustCatalogStructure;
    WintrustStructure.dwStateAction = WTD_STATEACTION_IGNORE;  // Ignore hWVTStateData
    WintrustStructure.dwProvFlags = WTD_REVOCATION_CHECK_NONE;
    WintrustStructure.dwUIContext = 0L;

    // WinVerifyTrust verifies signatures as specified by the GUID and Wintrust_Data.
    LONG lStatus = m_wintrust.WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &WVTPolicyGUID, &WintrustStructure);

    // TODO: why here ? why not lazy ? CertStores ?
    {
        fmt::basic_memory_buffer<char, 65536> catalogData;
        auto rv = ::MapFile(InfoStruct.wszCatalogFile, catalogData);
        if (rv.has_error())
        {
            Log::Error(
                L"Failed to extract signature information from catalog '{}' [{}]",
                InfoStruct.wszCatalogFile,
                rv.error());
            return ToHRESULT(rv.error());
        }

        HRESULT hr = ExtractCatalogSigners(
            InfoStruct.wszCatalogFile, std::string_view(catalogData.data(), catalogData.size()), data);
        if (FAILED(hr))
        {
            Log::Debug(L"Failed to extract signer information from catalog '{}'", InfoStruct.wszCatalogFile);
        }
    }

    if (FAILED(EvaluateCheck(lStatus, data)))
    {
        Log::Debug(L"Failed to evaluate result of signature check '{}'", InfoStruct.wszCatalogFile);
    }

    if (data.isSigned && data.bSignatureVerifies)
        data.AuthStatus = Authenticode::AUTHENTICODE_CATALOG_SIGNED_VERIFIED;
    else if (data.isSigned)
        data.AuthStatus = Authenticode::AUTHENTICODE_SIGNED_NOT_VERIFIED;
    return S_OK;
}

HRESULT Authenticode::EvaluateCheck(LONG lStatus, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;

    DWORD dwLastError = GetLastError();

    switch (lStatus)
    {
        case ERROR_SUCCESS:
            /*
            Signed file:
            - Hash that represents the subject is trusted.

            - Trusted publisher without any verification errors.

            - UI was disabled in dwUIChoice. No publisher or
            time stamp chain errors.

            - UI was enabled in dwUIChoice and the user clicked
            "Yes" when asked to install and run the signed
            subject.
            */
            data.isSigned = true;
            data.bSignatureVerifies = true;
            data.AuthStatus = AUTHENTICODE_CATALOG_SIGNED_VERIFIED;
            hr = S_OK;
            break;

        case TRUST_E_NOSIGNATURE:
            // The file was not signed or had a signature
            // that was not valid.

            // Get the reason for no signature.
            if (TRUST_E_NOSIGNATURE == dwLastError || TRUST_E_SUBJECT_FORM_UNKNOWN == dwLastError
                || TRUST_E_PROVIDER_UNKNOWN == dwLastError)
            {
                // The file was not signed.
                Log::Debug("The file is not signed.");
                hr = S_OK;
            }
            else
            {
                // The signature was not valid or there was an error
                // opening the file.
                hr = HRESULT_FROM_WIN32(dwLastError);
                Log::Debug("An unknown error occurred trying to verify the signature [{}]", SystemError(hr));
            }
            break;

        case TRUST_E_EXPLICIT_DISTRUST:
            // The hash that represents the subject or the publisher
            // is not allowed by the admin or user.
            Log::Debug("The signature is present, but specifically disallowed");
            data.isSigned = true;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
            hr = S_OK;
            break;

        case TRUST_E_SUBJECT_NOT_TRUSTED:
            // The user clicked "No" when asked to install and run.
            Log::Debug("The signature is present, but not trusted");
            data.isSigned = true;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
            hr = S_OK;
            break;

        case CRYPT_E_SECURITY_SETTINGS:
            /*
            The hash that represents the subject or the publisher
            was not explicitly trusted by the admin and the
            admin policy has disabled user trust. No signature,
            publisher or time stamp errors.
            */
            Log::Debug(
                "CRYPT_E_SECURITY_SETTINGS - The hash "
                "representing the subject or the publisher wasn't "
                "explicitly trusted by the admin and admin policy "
                "has disabled user trust. No signature, publisher "
                "or timestamp errors.");
            data.isSigned = true;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
            hr = S_OK;
            break;

        default:
            // The UI was disabled in dwUIChoice or the admin policy
            // has disabled user trust. lStatus contains the
            // publisher or time stamp chain error.
            Log::Debug("Error is: {:#x}", lStatus);
            data.isSigned = false;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_NOT_SIGNED;
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
    }
    return hr;
}

HRESULT Authenticode::Verify(LPCWSTR path, AuthenticodeData& data)
{
    auto stream = std::make_shared<FileStream>();
    HRESULT hr = stream->OpenFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to open file '{}' [{}]", path, SystemError(hr));
        return hr;
    }

    return Verify(path, stream, data);
}

HRESULT Authenticode::Verify(LPCWSTR szFileName, const std::shared_ptr<ByteStream>& pStream, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;
    std::error_code ec;

    CacheStream cache(*pStream, 1048576);
    PeParser pe(cache, ec);
    if (ec)
    {
        Log::Debug(L"Failed to parse pe file (filename: {}) [{}]", szFileName, ec);
        return ec.value();
    }

    if (pe.HasSecurityDirectory())
    {
        PeParser::PeHash hashes;

        // Exclude 'CryptoHashStream::Algorithm::MD5' as algorithm is untrusted
        pe.GetAuthenticodeHash(CryptoHashStream::Algorithm::SHA1 | CryptoHashStream::Algorithm::SHA256, hashes, ec);
        if (ec)
        {
            Log::Debug("Failed to compute pe hashes [{}]", ec);
            return ec.value();
        }

        std::vector<uint8_t> securityDirectory;
        pe.ReadSecurityDirectory(securityDirectory, ec);
        if (ec)
        {
            Log::Debug("Failed to read security directory [{}]", ec);
            return ec.value();
        }

        CBinaryBuffer bb;
        bb.SetCount(securityDirectory.size());
        std::copy(std::cbegin(securityDirectory), std::cend(securityDirectory), bb.GetData());
        return Verify(szFileName, bb, To_PE_Hashs(hashes), data);
    }
    else
    {
        // This will get to know which algorithm is used inside catalog so only required signature will be processed
        static const DWORD dwRequestedHashSize = Authenticode::ExpectedHashSize();
        static const auto algorithms = dwRequestedHashSize == BYTES_IN_SHA1_HASH ? CryptoHashStream::Algorithm::SHA1
                                                                                 : CryptoHashStream::Algorithm::SHA256;
        PeParser::PeHash hashes;
        pe.GetAuthenticodeHash(algorithms, hashes, ec);
        if (ec)
        {
            Log::Debug("Failed to compute pe hashes [{}]", ec);
            return ec.value();
        }

        return VerifyAnySignatureWithCatalogs(szFileName, To_PE_Hashs(hashes), data);
    }
}

HRESULT Authenticode::VerifyAnySignatureWithCatalogs(LPCWSTR szFileName, const PE_Hashs& hashs, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;

    data.isSigned = false;
    data.bSignatureVerifies = false;

    HCATINFO hCatalog = INVALID_HANDLE_VALUE;
    bool bIsCatalogSigned = false;

    if (hCatalog == INVALID_HANDLE_VALUE && hashs.sha256.GetCount())
    {
        if (FAILED(hr = FindCatalogForHash(hashs.sha256, bIsCatalogSigned, hCatalog)))
        {
            Log::Debug("Could not find a catalog for SHA256 hash [{}]", SystemError(hr));
        }
        else if (bIsCatalogSigned)
        {
            // Only if file is catalog signed and hash was passed, proceed with verification
            hr = VerifySignatureWithCatalogs(szFileName, hashs.sha256, hCatalog, data);

            if (!CryptCATAdminReleaseCatalogContext(m_hContext, hCatalog, 0))
            {
                Log::Error("Failed CryptCATAdminReleaseCatalogContext [{}]", LastWin32Error());
            }

            return hr;
        }
    }

    if (hCatalog == INVALID_HANDLE_VALUE && hashs.sha1.GetCount())
    {
        if (FAILED(hr = FindCatalogForHash(hashs.sha1, bIsCatalogSigned, hCatalog)))
        {
            Log::Debug("Could not find a catalog for SHA1 hash [{}]", SystemError(hr));
        }
        else if (bIsCatalogSigned)
        {
            // Only if file is catalog signed and hash was passed, proceed with verification
            hr = VerifySignatureWithCatalogs(szFileName, hashs.sha1, hCatalog, data);

            if (!CryptCATAdminReleaseCatalogContext(m_hContext, hCatalog, 0))
            {
                Log::Error("Failed CryptCATAdminReleaseCatalogContext [{}]", LastWin32Error());
            }

            return hr;
        }
    }

    if (hashs.md5.GetCount())
    {
        if (FAILED(hr = FindCatalogForHash(hashs.md5, bIsCatalogSigned, hCatalog)))
        {
            Log::Debug(L"Could not find a catalog for MD5 hash [{}]", SystemError(hr));
        }
        else if (bIsCatalogSigned)
        {
            // Only if file is catalog signed and hash was passed, proceed with verification
            hr = VerifySignatureWithCatalogs(szFileName, hashs.md5, hCatalog, data);

            if (!CryptCATAdminReleaseCatalogContext(m_hContext, hCatalog, 0))
            {
                Log::Error("Failed CryptCATAdminReleaseCatalogContext [{}]", LastWin32Error());
            }

            return hr;
        }
    }

    data.bSignatureVerifies = false;
    data.isSigned = false;
    data.AuthStatus = AUTHENTICODE_NOT_SIGNED;
    return S_OK;
}

HRESULT Orc::Authenticode::ExtractSignatureSize(const CBinaryBuffer& signature, DWORD& cbSize)
{
    HRESULT hr = E_FAIL;
    DWORD dwContentType = 0L;
    HCRYPTMSG hMsg = NULL;
    DWORD dwMsgAndCertEncodingType = 0L;
    DWORD dwFormatType = 0;

    cbSize = 0L;

    CRYPT_DATA_BLOB blob;
    blob.cbData = (DWORD)signature.GetCount();
    blob.pbData = signature.GetData();

    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_BLOB,
            &blob,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0L,
            &dwMsgAndCertEncodingType,
            &dwContentType,
            &dwFormatType,
            NULL,
            &hMsg,
            NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed CryptQueryObject [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hMsg))
    {
        CryptMsgClose(hMsg);
    }
    BOOST_SCOPE_EXIT_END;

    if (dwContentType == CERT_QUERY_CONTENT_PKCS7_SIGNED && dwFormatType == CERT_QUERY_FORMAT_BINARY)
    {
        // Expected message type: PKCS7_SIGNED in binary format
        DWORD dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_CONTENT_PARAM, 0, NULL, &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed to query authenticode content info [{}]", SystemError(hr));
            return hr;
        }
        cbSize = dwBytes;
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT Authenticode::ExtractSignatureHash(const CBinaryBuffer& signature, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;
    DWORD dwContentType = 0L;
    HCRYPTMSG hMsg = NULL;
    DWORD dwMsgAndCertEncodingType = 0L;
    DWORD dwFormatType = 0;

    CRYPT_DATA_BLOB blob;
    blob.cbData = (DWORD)signature.GetCount();
    blob.pbData = signature.GetData();

    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_BLOB,
            &blob,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0L,
            &dwMsgAndCertEncodingType,
            &dwContentType,
            &dwFormatType,
            NULL,
            &hMsg,
            NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed CryptQueryObject [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hMsg))
    {
        CryptMsgClose(hMsg);
    }
    BOOST_SCOPE_EXIT_END;

    if (dwContentType == CERT_QUERY_CONTENT_PKCS7_SIGNED && dwFormatType == CERT_QUERY_FORMAT_BINARY)
    {
        // Expected message type: PKCS7_SIGNED in binary format
        DWORD dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_CONTENT_PARAM, 0, NULL, &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to query authenticode content info [{}]", SystemError(hr));
            return hr;
        }
        CBinaryBuffer msgContent;
        msgContent.SetCount(dwBytes);

        if (!CryptMsgGetParam(hMsg, CMSG_CONTENT_PARAM, 0, msgContent.GetData(), &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to get authenticode content info [{}]", SystemError(hr));
            return hr;
        }

        dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, NULL, &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to query authenticode content type info [{}]", SystemError(hr));
            return hr;
        }

        dwBytes = 0L;
        if (!CryptDecodeObject(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                SPC_INDIRECT_DATA_OBJID,
                msgContent.GetData(),
                (DWORD)msgContent.GetCount(),
                CRYPT_DECODE_NOCOPY_FLAG,
                NULL,
                &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to decode authenticode SPC_INDIRECT_DATA info length [{}]", SystemError(hr));
            return hr;
        }

        CBinaryBuffer decodedIndirectData;
        if (!decodedIndirectData.SetCount(dwBytes))
            return E_OUTOFMEMORY;

        if (!CryptDecodeObject(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                SPC_INDIRECT_DATA_OBJID,
                msgContent.GetData(),
                (DWORD)msgContent.GetCount(),
                CRYPT_DECODE_NOCOPY_FLAG,
                decodedIndirectData.GetData(),
                &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to decode authenticode SPC_INDIRECT_DATA info [{}]", SystemError(hr));
            return hr;
        }
        PSPC_INDIRECT_DATA_CONTENT pIndirectData = (PSPC_INDIRECT_DATA_CONTENT)decodedIndirectData.GetData();

        if (!strcmp(pIndirectData->DigestAlgorithm.pszObjId, szOID_OIWSEC_sha1))
        {
            data.SignedHashs.sha1.SetData(pIndirectData->Digest.pbData, pIndirectData->Digest.cbData);
        }
        else if (!strcmp(pIndirectData->DigestAlgorithm.pszObjId, szOID_NIST_sha256))
        {
            data.SignedHashs.sha256.SetData(pIndirectData->Digest.pbData, pIndirectData->Digest.cbData);
        }
        else if (!strcmp(pIndirectData->DigestAlgorithm.pszObjId, szOID_RSA_MD5))
        {
            data.SignedHashs.md5.SetData(pIndirectData->Digest.pbData, pIndirectData->Digest.cbData);
        }
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT Authenticode::ExtractSignatureTimeStamp(const CBinaryBuffer& signature, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;
    DWORD dwContentType = 0L;
    HCRYPTMSG hMsg = NULL;
    DWORD dwMsgAndCertEncodingType = 0L;
    DWORD dwFormatType = 0;

    CRYPT_DATA_BLOB blob;
    blob.cbData = (DWORD)signature.GetCount();
    blob.pbData = signature.GetData();

    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_BLOB,
            &blob,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0L,
            &dwMsgAndCertEncodingType,
            &dwContentType,
            &dwFormatType,
            NULL,
            &hMsg,
            NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed CryptQueryObject [{}]", SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hMsg))
    {
        CryptMsgClose(hMsg);
    }
    BOOST_SCOPE_EXIT_END;

    if (dwContentType == CERT_QUERY_CONTENT_PKCS7_SIGNED && dwFormatType == CERT_QUERY_FORMAT_BINARY)
    {
        // Expected message type: PKCS7_SIGNED in binary format
        DWORD dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to query authenticode content info [{}]", SystemError(hr));
            return hr;
        }
        CBinaryBuffer msgContent;
        msgContent.SetCount(dwBytes);

        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, msgContent.GetData(), &dwBytes))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to get authenticode content info [{}]", SystemError(hr));
            return hr;
        }

        PCMSG_SIGNER_INFO pSignerInfo = msgContent.GetP<CMSG_SIGNER_INFO>(0);

        // Loop through unathenticated attributes for
        // szOID_RSA_counterSign OID.
        for (DWORD n = 0; n < pSignerInfo->UnauthAttrs.cAttr; n++)
        {
            if (lstrcmpA(pSignerInfo->UnauthAttrs.rgAttr[n].pszObjId, szOID_RSA_counterSign) == 0)
            {
                // Get size of CMSG_SIGNER_INFO structure.
                DWORD dwSize = 0;
                if (!CryptDecodeObject(
                        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                        PKCS7_SIGNER_INFO,
                        pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                        pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
                        0,
                        NULL,
                        &dwSize))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Error("Failed CryptDecodeObject [{}]", SystemError(hr));
                    return hr;
                }

                // Allocate memory for CMSG_SIGNER_INFO.
                CBinaryBuffer buffer;
                buffer.SetCount(dwSize);

                // Decode and get CMSG_SIGNER_INFO structure
                // for timestamp certificate.
                if (!CryptDecodeObject(
                        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                        PKCS7_SIGNER_INFO,
                        pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                        pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
                        0,
                        buffer.GetData(),
                        &dwSize))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Error("Failed CryptDecodeObject [{}]", SystemError(hr));
                    return hr;
                }

                PCMSG_SIGNER_INFO pCounterSignerInfo = buffer.GetP<CMSG_SIGNER_INFO>(0);

                // Loop through authenticated attributes and find
                // szOID_RSA_signingTime OID.
                for (DWORD m = 0; m < pCounterSignerInfo->AuthAttrs.cAttr; m++)
                {
                    if (lstrcmpA(szOID_RSA_signingTime, pCounterSignerInfo->AuthAttrs.rgAttr[m].pszObjId) == 0)
                    {
                        // Decode and get FILETIME structure.
                        DWORD dwData = sizeof(FILETIME);
                        if (!CryptDecodeObject(
                                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                szOID_RSA_signingTime,
                                pCounterSignerInfo->AuthAttrs.rgAttr[m].rgValue[0].pbData,
                                pCounterSignerInfo->AuthAttrs.rgAttr[m].rgValue[0].cbData,
                                0,
                                (PVOID)&data.Timestamp,
                                &dwData))
                        {
                            hr = HRESULT_FROM_WIN32(GetLastError());
                            Log::Debug("Failed CryptDecodeObject [{}]", SystemError(hr));
                            break;
                        }
                        break;  // Break from for loop.
                    }
                }
            }
        }
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT Authenticode::ExtractNestedSignature(
    HCRYPTMSG hMsg,
    DWORD dwSignerIndex,
    std::vector<PCCERT_CONTEXT>& pSigners,
    std::vector<HCERTSTORE>& certStores)
{
    HRESULT hr = E_FAIL;
    DWORD cbNeeded = 0L;

    if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, dwSignerIndex, NULL, &cbNeeded))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());

        if (hr == CRYPT_E_ATTRIBUTES_MISSING)
        {
            // There is no nested signature information to retrieve
            return S_OK;
        }

        Log::Error("Failed to retrieve nested signature size [{}]", SystemError(hr));
        return hr;
    }

    CBinaryBuffer crypt_attributes;
    crypt_attributes.SetCount(cbNeeded);

    if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, dwSignerIndex, crypt_attributes.GetData(), &cbNeeded))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to retrieve nested signature [{}]", SystemError(hr));
        return hr;
    }

#ifndef szOID_NESTED_SIGNATURE
    static const LPSTR szOID_NESTED_SIGNATURE = "1.3.6.1.4.1.311.2.4.1";
#endif

    PCRYPT_ATTRIBUTES pAttrs = crypt_attributes.GetP<CRYPT_ATTRIBUTES>();
    for (UINT i = 0; i < pAttrs->cAttr; i++)
    {
        CRYPT_ATTRIBUTE& pAttr = pAttrs->rgAttr[i];

        if (!strcmp(pAttr.pszObjId, szOID_NESTED_SIGNATURE))
        {
            Log::Trace("Found one nested signature");

            DWORD dwEncoding, dwContentType, dwFormatType;
            HCERTSTORE hCertStore = NULL;
            HCRYPTMSG hMsg = NULL;

            CRYPT_DATA_BLOB blob;
            blob.cbData = (DWORD)pAttr.rgValue->cbData;
            blob.pbData = pAttr.rgValue->pbData;

            if (!CryptQueryObject(
                    CERT_QUERY_OBJECT_BLOB,
                    &blob,
                    CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
                    CERT_QUERY_FORMAT_FLAG_BINARY,
                    0L,
                    &dwEncoding,
                    &dwContentType,
                    &dwFormatType,
                    &hCertStore,
                    &hMsg,
                    NULL))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error("Failed CryptDecodeObject [{}]", SystemError(hr));
                return hr;
            }
            BOOST_SCOPE_EXIT((&hMsg))
            {
                CryptMsgClose(hMsg);
            }
            BOOST_SCOPE_EXIT_END;

            certStores.push_back(hCertStore);

            PCCERT_CONTEXT pSigner = NULL;
            DWORD dwSignerIndex = 0;

            while (CryptMsgGetAndVerifySigner(
                hMsg, 1, &hCertStore, CMSG_USE_SIGNER_INDEX_FLAG | CMSG_SIGNER_ONLY_FLAG, &pSigner, &dwSignerIndex))
            {
                pSigners.push_back(CertDuplicateCertificateContext(pSigner));
                CertFreeCertificateContext(pSigner);

                ExtractNestedSignature(hMsg, dwSignerIndex, pSigners, certStores);

                dwSignerIndex++;
            }

            hr = HRESULT_FROM_WIN32(GetLastError());
            if (hr != CRYPT_E_INVALID_INDEX)
            {
                Log::Error("Failed to extract signer information from blob [{}]", SystemError(hr));
                return hr;
            }
        }
    }
    return S_OK;
}

HRESULT Authenticode::ExtractSignatureSigners(
    const CBinaryBuffer& signature,
    const FILETIME& timestamp,
    std::vector<PCCERT_CONTEXT>& pSigners,
    std::vector<PCCERT_CONTEXT>& pCAs,
    std::vector<HCERTSTORE>& certStores)
{
    HRESULT hr = E_FAIL;

    DWORD dwEncoding, dwContentType, dwFormatType;
    HCERTSTORE hCertStore = NULL;
    HCRYPTMSG hMsg = NULL;

    CRYPT_DATA_BLOB blob;
    blob.cbData = (DWORD)signature.GetCount();
    blob.pbData = signature.GetData();

    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_BLOB,
            &blob,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0L,
            &dwEncoding,
            &dwContentType,
            &dwFormatType,
            &hCertStore,
            &hMsg,
            NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed CryptDecodeObject [{}]", SystemError(hr));
        return hr;
    }

    certStores.push_back(hCertStore);

    BOOST_SCOPE_EXIT((&hMsg))
    {
        CryptMsgClose(hMsg);
    }
    BOOST_SCOPE_EXIT_END;

    PCCERT_CONTEXT pSigner = NULL;
    DWORD dwSignerIndex = 0;

    while (CryptMsgGetAndVerifySigner(
        hMsg, 1, &hCertStore, CMSG_USE_SIGNER_INDEX_FLAG | CMSG_SIGNER_ONLY_FLAG, &pSigner, &dwSignerIndex))
    {
        pSigners.push_back(CertDuplicateCertificateContext(pSigner));
        CertFreeCertificateContext(pSigner);

        ExtractNestedSignature(hMsg, dwSignerIndex, pSigners, certStores);

        dwSignerIndex++;
    }
    if (GetLastError() != CRYPT_E_INVALID_INDEX)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed to extract signer information from blob [{}]", SystemError(hr));
        return hr;
    }

    for (const auto& signer : pSigners)
    {
        PCCERT_CHAIN_CONTEXT pChain = nullptr;

        CERT_ENHKEY_USAGE EnhkeyUsage;
        EnhkeyUsage.cUsageIdentifier = 0;
        EnhkeyUsage.rgpszUsageIdentifier = NULL;
        CERT_USAGE_MATCH CertUsage;
        CertUsage.dwType = USAGE_MATCH_TYPE_AND;
        CertUsage.Usage = EnhkeyUsage;
        CERT_CHAIN_PARA params;
        ZeroMemory(&params, sizeof(params));
        params.cbSize = sizeof(params);
        params.RequestedUsage = CertUsage;

        if (!CertGetCertificateChain(
                HCCE_LOCAL_MACHINE,
                signer,
                const_cast<PFILETIME>(&timestamp),
                hCertStore,
                &params,
                CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY,
                NULL,
                &pChain))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to obtain certificate chain [{}]", SystemError(hr));
            break;
        }
        BOOST_SCOPE_EXIT(pChain)
        {
            CertFreeCertificateChain(pChain);
        }
        BOOST_SCOPE_EXIT_END;

        // Go through each simple chain to obtain all root CAs
        for (UINT cIdx = 0; cIdx < pChain->cChain; cIdx++)
        {
            PCERT_SIMPLE_CHAIN pSimpleChain = pChain->rgpChain[cIdx];
            if (pSimpleChain->cElement > 0)
            {
                pCAs.push_back(CertDuplicateCertificateContext(
                    pSimpleChain->rgpElement[pSimpleChain->cElement - 1]->pCertContext));
            }
        }
    }
    return S_OK;
}

HRESULT
Authenticode::ExtractCatalogSigners(std::wstring_view catalogPath, std::string_view catalogData, AuthenticodeData& data)
{
    if (data.SignersInfo())
    {
        return S_OK;
    }

    if (data.AuthenticodeCache())
    {
        auto info = data.AuthenticodeCache()->Find(catalogPath);
        if (info)
        {
            data.SetSignerInfo(std::move(info));
            return S_OK;
        }
    }

    HRESULT hr = ExtractCatalogSigners(catalogData, data.Signers(), data.SignersCAs(), data.CertStores);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to extract signer information from catalog '{}'", catalogPath);
        return hr;
    }

    if (data.AuthenticodeCache())
    {
        auto signersInfo =
            ::UpdateSignersCache(*data.AuthenticodeCache(), catalogPath, data.Signers(), data.SignersCAs());
        data.SetSignerInfo(std::move(signersInfo));
    }

    return S_OK;
}

HRESULT Authenticode::ExtractCatalogSigners(
    std::string_view catalogData,
    std::vector<PCCERT_CONTEXT>& pSigners,
    std::vector<PCCERT_CONTEXT>& pCAs,
    std::vector<HCERTSTORE>& certStores)
{
    HRESULT hr = E_FAIL;

    DWORD dwEncoding, dwContentType, dwFormatType;
    HCERTSTORE hCertStore = NULL;
    HCRYPTMSG hMsg = NULL;

    {
        CRYPT_DATA_BLOB blob;
        blob.cbData = catalogData.size();
        blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(catalogData.data()));

        if (!CryptQueryObject(
                CERT_QUERY_OBJECT_BLOB,
                &blob,
                CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                CERT_QUERY_FORMAT_FLAG_BINARY,
                0,
                &dwEncoding,
                &dwContentType,
                &dwFormatType,
                &hCertStore,
                &hMsg,
                NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            return hr;
        }
    }

    BOOST_SCOPE_EXIT((&hMsg))
    {
        CryptMsgClose(hMsg);
    }
    BOOST_SCOPE_EXIT_END;

    certStores.push_back(hCertStore);

    PCCERT_CONTEXT pSigner = NULL;
    DWORD dwSignerIndex = 0;

    while (CryptMsgGetAndVerifySigner(hMsg, 1, &hCertStore, CMSG_USE_SIGNER_INDEX_FLAG, &pSigner, &dwSignerIndex))
    {
        pSigners.push_back(CertDuplicateCertificateContext(pSigner));
        ExtractNestedSignature(hMsg, dwSignerIndex, pSigners, certStores);
        dwSignerIndex++;
    }
    if (GetLastError() != CRYPT_E_INVALID_INDEX)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        return hr;
    }

    for (const auto& signer : pSigners)
    {
        PCCERT_CHAIN_CONTEXT pChain = nullptr;

        CERT_ENHKEY_USAGE EnhkeyUsage;
        EnhkeyUsage.cUsageIdentifier = 0;
        EnhkeyUsage.rgpszUsageIdentifier = NULL;
        CERT_USAGE_MATCH CertUsage;
        CertUsage.dwType = USAGE_MATCH_TYPE_AND;
        CertUsage.Usage = EnhkeyUsage;
        CERT_CHAIN_PARA params;
        ZeroMemory(&params, sizeof(params));
        params.cbSize = sizeof(params);
        params.RequestedUsage = CertUsage;

        if (!CertGetCertificateChain(HCCE_LOCAL_MACHINE, signer, NULL, NULL, &params, 0L, NULL, &pChain))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed to obtain certificate chain [{}]", SystemError(hr));
            return hr;
        }
        BOOST_SCOPE_EXIT(pChain)
        {
            CertFreeCertificateChain(pChain);
        }
        BOOST_SCOPE_EXIT_END;

        // Go through each simple chain to obtain all root CAs
        for (UINT cIdx = 0; cIdx < pChain->cChain; cIdx++)
        {
            PCERT_SIMPLE_CHAIN pSimpleChain = pChain->rgpChain[cIdx];
            if (pSimpleChain->cElement > 0)
            {
                pCAs.push_back(CertDuplicateCertificateContext(
                    pSimpleChain->rgpElement[pSimpleChain->cElement - 1]->pCertContext));
            }
        }
    }

    return S_OK;
}

HRESULT
Authenticode::Verify(LPCWSTR szFileName, const CBinaryBuffer& secdir, const PE_Hashs& hashs, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;
    // this PE has a security directory, the file is signed
    LPWIN_CERTIFICATE pWin = (LPWIN_CERTIFICATE)secdir.GetData();

    std::wstringstream SignerStream;
    DWORD dwIndex = 0;

    data.isSigned = false;
    data.bSignatureVerifies = false;

    while (dwIndex < secdir.GetCount())
    {
        if (pWin->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA && pWin->wRevision == WIN_CERT_REVISION_2_0)
        {
            CBinaryBuffer signature(pWin->bCertificate, pWin->dwLength);

            data.isSigned = true;

            if (FAILED(hr = ExtractSignatureHash(signature, data)))
            {
                Log::Error(
                    L"Failed to extract hash from signature in the security directory of '{}' [{}]",
                    szFileName,
                    SystemError(hr));
            }
            if (hashs.md5.GetCount() != 0 && hashs.md5.GetCount() == data.SignedHashs.md5.GetCount())
            {
                if (!memcmp(hashs.md5.GetData(), data.SignedHashs.md5.GetData(), hashs.md5.GetCount()))
                {
                    data.AuthStatus = AUTHENTICODE_SIGNED_VERIFIED;
                    data.bSignatureVerifies = true;
                }
                else
                {
                    data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
                    data.bSignatureVerifies = false;
                }
            }
            else if (hashs.sha1.GetCount() != 0 && hashs.sha1.GetCount() == data.SignedHashs.sha1.GetCount())
            {
                if (!memcmp(hashs.sha1.GetData(), data.SignedHashs.sha1.GetData(), hashs.sha1.GetCount()))
                {
                    data.AuthStatus = AUTHENTICODE_SIGNED_VERIFIED;
                    data.bSignatureVerifies = true;
                }
                else
                {
                    data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
                    data.bSignatureVerifies = false;
                }
            }
            else if (hashs.sha256.GetCount() != 0 && hashs.sha256.GetCount() == data.SignedHashs.sha256.GetCount())
            {
                if (!memcmp(hashs.sha256.GetData(), data.SignedHashs.sha256.GetData(), hashs.sha256.GetCount()))
                {
                    data.AuthStatus = AUTHENTICODE_SIGNED_VERIFIED;
                    data.bSignatureVerifies = true;
                }
                else
                {
                    data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
                    data.bSignatureVerifies = false;
                }
            }
            else
            {
                Log::Warn(L"Signature could not be verified '{}'", szFileName);
                data.bSignatureVerifies = false;
            }

            FILETIME timestamp = {0L, 0L};

            if (FAILED(hr = ExtractSignatureTimeStamp(signature, data)))
            {
                Log::Error(
                    L"Failed to extract timestamp information from signature in the security directory of '{}' [{}]",
                    szFileName,
                    SystemError(hr));
            }

            hr = ExtractSignatureSigners(signature, data.Timestamp, data.Signers(), data.SignersCAs(), data.CertStores);
            if (FAILED(hr))
            {
                Log::Error(
                    L"Failed to extract signer info from signature from security directory of '{}' [{}]",
                    szFileName,
                    SystemError(hr));
            }
        }

        if (pWin->dwLength > 0)
        {
            dwIndex += pWin->dwLength;
            pWin = (LPWIN_CERTIFICATE)(secdir.GetData() + dwIndex);
        }
        else
            break;
    }
    return S_OK;
}

HRESULT Orc::Authenticode::SignatureSize(LPCWSTR szFileName, const CBinaryBuffer& secdir, DWORD& cbSize)
{
    HRESULT hr = E_FAIL;
    // this PE has a security directory, the file is signed

    // init return value
    cbSize = 0;

    LPWIN_CERTIFICATE pWin = (LPWIN_CERTIFICATE)secdir.GetData();
    DWORD dwIndex = 0;

    while (dwIndex < secdir.GetCount())
    {
        if (pWin->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA && pWin->wRevision == WIN_CERT_REVISION_2_0)
        {
            CBinaryBuffer signature(pWin->bCertificate, pWin->dwLength);

            DWORD dwSignatureSize = 0L;
            if (FAILED(hr = ExtractSignatureSize(signature, dwSignatureSize)))
            {
                Log::Error(
                    L"Failed to extract hash from signature in the security directory of '{}' [{}]",
                    szFileName,
                    SystemError(hr));
            }
            else
                cbSize += dwSignatureSize;
        }

        if (pWin->dwLength > 0)
        {
            dwIndex += pWin->dwLength;
            pWin = (LPWIN_CERTIFICATE)(secdir.GetData() + dwIndex);
        }
        else
            break;
    }
    return S_OK;
}

Authenticode::~Authenticode()
{
    if (m_hContext != INVALID_HANDLE_VALUE)
    {
        BOOL ret = m_wintrust.CryptCATAdminReleaseContext(m_hContext, 0);
        if (ret == FALSE)
        {
            Log::Error("Failed CryptCATAdminReleaseContext [{}]", LastWin32Error());
        }
    }

    if (m_hMachineStore != INVALID_HANDLE_VALUE)
    {
        BOOL ret = CertCloseStore(m_hMachineStore, 0L);
        if (ret == FALSE)
        {
            Log::Error("Failed CertCloseStore [{}]", LastWin32Error());
        }
    }
}

const std::vector<std::wstring>& Authenticode::AuthenticodeData::SignersName()
{
    if (m_signersInfo)
    {
        return m_signersInfo->names;
    }

    if (!m_signersName)
    {
        std::vector<std::wstring> names;
        GetSignersName(m_signers, names);
        m_signersName = std::move(names);
    }

    return *m_signersName;
}

const std::vector<Authenticode::Thumbprint>& Authenticode::AuthenticodeData::SignersThumbprint()
{
    if (m_signersInfo)
    {
        return m_signersInfo->thumbprints;
    }

    if (!m_signersThumbprint)
    {
        std::vector<Thumbprint> thumbprint;
        GetSignersThumbprint(m_signers, thumbprint);
        m_signersThumbprint = std::move(thumbprint);
    }

    return *m_signersThumbprint;
}

const std::vector<std::wstring>& Authenticode::AuthenticodeData::SignersCertificateAuthoritiesName()
{
    if (m_signersInfo)
    {
        return m_signersInfo->certificateAuthoritiesName;
    }

    if (!m_signersCertificateAuthoritiesName)
    {
        std::vector<std::wstring> names;
        GetSignersName(m_signersCertificateAuthorities, names);
        m_signersCertificateAuthoritiesName = std::move(names);
    }

    return *m_signersCertificateAuthoritiesName;
}

const std::vector<Authenticode::Thumbprint>& Authenticode::AuthenticodeData::SignersCertificateAuthoritiesThumbprint()
{
    if (m_signersInfo)
    {
        return m_signersInfo->certificateAuthoritiesThumbprint;
    }

    if (!m_signersCertificateAuthoritiesThumbprint)
    {
        std::vector<Thumbprint> thumbprints;
        GetSignersThumbprint(m_signersCertificateAuthorities, thumbprints);
        m_signersCertificateAuthoritiesThumbprint = std::move(thumbprints);
    }

    return *m_signersCertificateAuthoritiesThumbprint;
}

Orc::Result<void> Authenticode::VerifySignatureWithCatalogHint(
    std::string_view catalogHint,
    const Orc::Authenticode::PE_Hashs& peHashes,
    Authenticode::AuthenticodeData& data)
{
    using namespace Orc;
    using namespace std::literals;

    data.bSignatureVerifies = false;

    auto filename = ParseCatalogHintFilename(catalogHint);
    if (filename.has_error())
    {
        Log::Debug("Failed to parse catalog hint filename [{}]", filename.error());
        return filename.error();
    }

    fmt::basic_memory_buffer<char, 65536> catalog;

    constexpr std::array catrootDirectories = {
        L"%WINDIR%\\system32\\CatRoot\\{{F750E6C3-38EE-11D1-85E5-00C04FC295EE}}\\{}"sv,
        L"%WINDIR%\\system32\\CatRoot\\{}"sv};

    for (auto catroot : catrootDirectories)
    {
        std::error_code ec;
        std::wstring path;
        fmt::format_to(std::back_inserter(path), catroot, filename.value());
        auto catalogPath = ExpandEnvironmentStringsApi(path.c_str(), ec);
        if (ec)
        {
            return ec;
        }

        auto rv = ::MapFile(catalogPath, catalog);
        if (rv.has_error())
        {
            Log::Error(L"Failed to map catalog '{}' [{}]", catalogPath, rv.error());
            return rv.error();
        }

        HRESULT hr = ExtractCatalogSigners(catalogPath, std::string_view(catalog.data(), catalog.size()), data);
        if (FAILED(hr))
        {
            return SystemError(hr);
        }

        break;
    }

    // TODO: eventually rely on this high level MS api wrapper instead of 'Authenticode::ExtractCatalogSigners'
    // auto signature = CheckCatalogSignature(std::string_view(catalog.data(), catalog.size()));
    // if (!signature)
    //{
    //    Log::Error("Failed catalog signature verification [{}]", signature.error());
    //    return signature.error();
    //}

    bool found = ::FindPeHash(std::string_view(catalog.data(), catalog.size()), peHashes);
    if (!found)
    {
        return std::errc::no_such_file_or_directory;
    }

    data.bSignatureVerifies = true;
    data.isSigned = true;
    data.AuthStatus = AUTHENTICODE_CATALOG_SIGNED_VERIFIED;

    return Success<void>();
}
