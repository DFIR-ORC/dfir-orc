//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "Authenticode.h"

#include "ByteStream.h"
#include "MemoryStream.h"
#include "CryptoHashStream.h"

#include "libpehash-pe.h"
#include "LogFileWriter.h"
#include "SystemDetails.h"

#include "WinTrustExtension.h"

#include <mscat.h>

#include <array>

#include <sstream>

#include <boost/scope_exit.hpp>

using namespace Orc;

static GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;

const FlagsDefinition Authenticode::AuthenticodeStatusDefs[] = {
    {AUTHENTICODE_UNKNOWN, L"Unknwon", L"This file status is unknown"},
    {AUTHENTICODE_NOT_PE, L"NotPE", L"This is not a PE"},
    {AUTHENTICODE_SIGNED_VERIFIED, L"SignedVerified", L"This PE is signed and signature verifies"},
    {AUTHENTICODE_CATALOG_SIGNED_VERIFIED, L"CatalogSignedVerified", L"This PE's hash is catalog signed"},
    {AUTHENTICODE_SIGNED_NOT_VERIFIED, L"SignedNotVerified", L"This PE's signature does not verify"},
    {AUTHENTICODE_NOT_SIGNED, L"NotSigned", L"This PE is not signed"},
    {(DWORD)-1, NULL, NULL}};

// TODO: failure should be handleable by caller
Authenticode::Authenticode(const logger& pLog)
    : m_wintrust(pLog)
    , _L_(pLog)
{
    HRESULT hr = E_FAIL;

    if (!m_wintrust.IsLoaded())
    {
        if (FAILED(hr = m_wintrust.Load()))
        {
            log::Warning(_L_, hr, L"Failed to load WinTrust\r\n");
            return;
        }
    }

    if (FAILED(hr=m_wintrust.CryptCATAdminAcquireContext(&m_hContext)))
    {
        log::Warning(_L_, hr, L"Failed to acquired context\r\n");
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
        log::Warning(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to open cert store\r\n");
        return;
    }
}

DWORD Authenticode::ExpectedHashSize(const logger& pLog)
{
    const auto wintrust = ExtensionLibrary::GetLibrary<WinTrustExtension>(pLog);

    WCHAR szPath[MAX_PATH];

    if (!ExpandEnvironmentStrings(L"%windir%\\System32\\kernel32.dll", szPath, MAX_PATH))
    {
        return (DWORD)-1;
    }

    HANDLE hFile = CreateFile(
        szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0L, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return (DWORD)-1;
    BOOST_SCOPE_EXIT(hFile) { CloseHandle(hFile); }
    BOOST_SCOPE_EXIT_END;

    HCATADMIN hContext = NULL;

    if (auto hr=wintrust->CryptCATAdminAcquireContext(&hContext); FAILED(hr))
    {
        return (DWORD)-1;
    }
    BOOST_SCOPE_EXIT(hContext) { CryptCATAdminReleaseContext(hContext, 0); }
    BOOST_SCOPE_EXIT_END;

    DWORD cbHash = 0L;
    if (auto hr=wintrust->CryptCATAdminCalcHashFromFileHandle(hFile, &cbHash, nullptr, 0); FAILED(hr))
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
            m_wintrust.CryptCATAdminReleaseCatalogContext(m_hContext, CatalogContext, 0);
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
Authenticode::VerifySignatureWithCatalogs(LPCWSTR szFileName, const CBinaryBuffer& hash, HCATINFO& hCatalog, AuthenticodeData& data)
{
    DBG_UNREFERENCED_PARAMETER(szFileName);
    CATALOG_INFO InfoStruct;
    // Zero our structures.
    memset(&InfoStruct, 0, sizeof(CATALOG_INFO));
    InfoStruct.cbStruct = sizeof(CATALOG_INFO);

    // If we couldn't get information
    if (!m_wintrust.CryptCATCatalogInfoFromContext(hCatalog, &InfoStruct, 0))
    {
        m_wintrust.CryptCATAdminReleaseCatalogContext(m_hContext, hCatalog, 0);
        hCatalog = NULL;
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring MemberTag = hash.ToHex();

    log::Verbose(_L_, L"The file is associated catalog with catalog %s.\r\n", InfoStruct.wszCatalogFile);

    WINTRUST_CATALOG_INFO WintrustCatalogStructure;
    ZeroMemory(&WintrustCatalogStructure, sizeof(WINTRUST_CATALOG_INFO));
    WintrustCatalogStructure.cbStruct = sizeof(WINTRUST_CATALOG_INFO);

    // Fill in catalog info structure.
    WintrustCatalogStructure.cbStruct = sizeof(WINTRUST_CATALOG_INFO);
    WintrustCatalogStructure.pcwszCatalogFilePath = InfoStruct.wszCatalogFile;
    WintrustCatalogStructure.cbCalculatedFileHash = (DWORD)hash.GetCount();
    WintrustCatalogStructure.pbCalculatedFileHash = hash.GetData();
    WintrustCatalogStructure.pcwszMemberTag = MemberTag.c_str();
    WintrustCatalogStructure.pcwszMemberFilePath = nullptr;
    WintrustCatalogStructure.hMemberFile = 0L;

    // If we get here, we have catalog info!  Verify it.
    WINTRUST_DATA WintrustStructure;
    ZeroMemory(&WintrustStructure, sizeof(WintrustStructure));
    WintrustStructure.cbStruct = sizeof(WINTRUST_DATA);
    WintrustStructure.dwUIChoice = WTD_UI_NONE;
    WintrustStructure.fdwRevocationChecks = WTD_REVOKE_NONE;
    WintrustStructure.dwUnionChoice = WTD_CHOICE_CATALOG;
    WintrustStructure.pCatalog = &WintrustCatalogStructure;
    WintrustStructure.dwStateAction = WTD_STATEACTION_VERIFY;
    WintrustStructure.dwProvFlags = WTD_REVOCATION_CHECK_NONE;
    WintrustStructure.dwUIContext = 0L;

    std::wstring strCatalog(InfoStruct.wszCatalogFile);
    auto iter = m_StateMap.find(strCatalog);
    if (iter != m_StateMap.end())
        WintrustStructure.hWVTStateData = iter->second;
    else
        WintrustStructure.hWVTStateData = NULL;

    // WinVerifyTrust verifies signatures as specified by the GUID
    // and Wintrust_Data.
    LONG lStatus = m_wintrust.WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &WVTPolicyGUID, &WintrustStructure);

    m_StateMap[std::move(strCatalog)] = WintrustStructure.hWVTStateData;

    if (!m_wintrust.CryptCATAdminReleaseCatalogContext(m_hContext, hCatalog, 0))
    {
        log::Verbose(_L_, L"The catalog %s was not successfully released.\r\n", InfoStruct.wszCatalogFile);
    }

    if (FAILED(ExtractCatalogSigners(InfoStruct.wszCatalogFile, data.Signers, data.SignersCAs)))
    {
        log::Verbose(_L_, L"Failed to extract signer information from catalog %s\r\n", InfoStruct.wszCatalogFile);
    }

    if (FAILED(EvaluateCheck(lStatus, data)))
    {
        log::Verbose(_L_, L"Failed to evaluate result of signature check %s\r\n", InfoStruct.wszCatalogFile);
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
                log::Verbose(_L_, L"The file is not signed.\r\n");
                hr = S_OK;
            }
            else
            {
                // The signature was not valid or there was an error
                // opening the file.
                log::Verbose(_L_, L"An unknown error occurred trying to verify the signature.\r\n");
                hr = HRESULT_FROM_WIN32(dwLastError);
            }
            break;

        case TRUST_E_EXPLICIT_DISTRUST:
            // The hash that represents the subject or the publisher
            // is not allowed by the admin or user.
            log::Verbose(_L_, L"The signature is present, but specifically disallowed.\r\n");
            data.isSigned = true;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
            hr = S_OK;
            break;

        case TRUST_E_SUBJECT_NOT_TRUSTED:
            // The user clicked "No" when asked to install and run.
            log::Verbose(_L_, L"The signature is present, but not trusted.\r\n");
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
            log::Verbose(
                _L_,
                L"CRYPT_E_SECURITY_SETTINGS - The hash "
                L"representing the subject or the publisher wasn't "
                L"explicitly trusted by the admin and admin policy "
                L"has disabled user trust. No signature, publisher "
                L"or timestamp errors.\r\n");
            data.isSigned = true;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_SIGNED_NOT_VERIFIED;
            hr = S_OK;
            break;

        default:
            // The UI was disabled in dwUIChoice or the admin policy
            // has disabled user trust. lStatus contains the
            // publisher or time stamp chain error.
            log::Verbose(_L_, L"Error is: 0x%lx.\r\n", lStatus);
            data.isSigned = false;
            data.bSignatureVerifies = false;
            data.AuthStatus = AUTHENTICODE_NOT_SIGNED;
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
    }
    return hr;
}

HRESULT Authenticode::Verify(LPCWSTR pwszSourceFile, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;

    data.isSigned = false;
    data.bSignatureVerifies = false;

    HANDLE hFile = INVALID_HANDLE_VALUE;

    // Open file.
    hFile = CreateFile(pwszSourceFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        return hr;
    }
    BOOST_SCOPE_EXIT(hFile) { CloseHandle(hFile); }
    BOOST_SCOPE_EXIT_END;

    CBinaryBuffer hash;

    // Get the size we need for our hash.
    DWORD HashSize = 0L;
	if (FAILED(hr= m_wintrust.CryptCATAdminCalcHashFromFileHandle(hFile, &HashSize, nullptr, 0L)))
		return hr;
    
	// Allocate memory.
    if (!hash.SetCount(HashSize))
        return E_OUTOFMEMORY;

    // Actually calculate the hash
	if (FAILED(hr = m_wintrust.CryptCATAdminCalcHashFromFileHandle(hFile, &HashSize, hash.GetData(), 0L)))
		return hr;

    PE_Hashs hashs;

    if (HashSize == BYTES_IN_MD5_HASH)
        hashs.md5 = hash;
    else if (HashSize == BYTES_IN_SHA1_HASH)
        hashs.sha1 = hash;
    else if (HashSize == BYTES_IN_SHA256_HASH)
        hashs.sha256 = hash;

    hr = VerifyAnySignatureWithCatalogs(pwszSourceFile, hashs, data);
    if (FAILED(hr))
    {
        return hr;
    }

    if (data.AuthStatus != AUTHENTICODE_NOT_SIGNED)
    {
        return S_OK;
    }

    return VerifyEmbeddedSignature(pwszSourceFile, hFile, data);
}

HRESULT Authenticode::Verify(LPCWSTR szFileName, const std::shared_ptr<ByteStream>& pStream, AuthenticodeData& data)
{
    HRESULT hr = E_FAIL;

    data.isSigned = false;
    data.bSignatureVerifies = false;

    if (pStream == nullptr)
        return E_POINTER;
    pStream->SetFilePointer(0L, FILE_BEGIN, NULL);

    // Load data in a memory stream

    auto memstream = std::make_shared<MemoryStream>(_L_);
    if (memstream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = memstream->OpenForReadWrite()))
        return hr;

    if (FAILED(hr = memstream->SetSize(pStream->GetSize())))
        return hr;

    ULONGLONG ullWritten = 0LL;
    if (FAILED(hr = pStream->CopyTo(*memstream, &ullWritten)))
        return hr;

    if ((ullWritten % 8) != 0)
    {
        // Apparently, MS padds PEs with zeroes on 8 modulo...
        DWORD dwToAdd = 8 - (ullWritten % 8);
        const char padding[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = memstream->Write((LPVOID)padding, dwToAdd, &ullWritten)))
            return hr;
    }
    memstream->Close();

    CBinaryBuffer pData;
    memstream->GrabBuffer(pData);
    typedef struct _PEChunks
    {
        uint32_t offset;
        uint32_t length;
    } PEChunks;

    const unsigned int MaxChunks = 256;
    PEChunks chunks[MaxChunks];
    ZeroMemory((BYTE*)chunks, sizeof(chunks));
    int cChunks = calc_pe_chunks_real(pData.GetData(), pData.GetCount(), (uint32_t*)chunks, MaxChunks);

    if (cChunks == -1)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    auto hashstream = std::make_shared<CryptoHashStream>(_L_);

    static DWORD dwRequestedHashSize = Authenticode::ExpectedHashSize(_L_);

    CryptoHashStream::Algorithm algs =
        dwRequestedHashSize == BYTES_IN_SHA1_HASH ? CryptoHashStream::Algorithm::SHA1 : CryptoHashStream::Algorithm::SHA256;

    hashstream->OpenToWrite(algs, nullptr);

    ULONGLONG ullHashed = 0LL;
    for (int i = 0; i < cChunks; ++i)
    {
        ULONGLONG ullThisWriteHashed = 0LL;
        if (FAILED(hr = hashstream->Write(pData.GetData() + chunks[i].offset, chunks[i].length, &ullThisWriteHashed)))
            return hr;
        ullHashed += ullThisWriteHashed;
    }

    PE_Hashs hashs;
    hashstream->GetMD5(hashs.md5);
    hashstream->GetSHA1(hashs.sha1);
    hashstream->GetSHA256(hashs.sha256);

    return VerifyAnySignatureWithCatalogs(szFileName, hashs, data);
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
            log::Verbose(_L_, L"Could not find a catalog for SHA256 hash\r\n");
        }
        else if (bIsCatalogSigned)
        {
            // Only if file is catalog signed and hash was passed, proceed with verification
            if (FAILED(hr = VerifySignatureWithCatalogs(szFileName, hashs.sha256, hCatalog, data)))
                return hr;
            return S_OK;
        }
    }

    if (hCatalog == INVALID_HANDLE_VALUE && hashs.sha1.GetCount())
    {
        if (FAILED(hr = FindCatalogForHash(hashs.sha1, bIsCatalogSigned, hCatalog)))
        {
            log::Verbose(_L_, L"Could not find a catalog for SHA1 hash\r\n");
        }
        else if (bIsCatalogSigned)
        {
            // Only if file is catalog signed and hash was passed, proceed with verification
            if (FAILED(hr = VerifySignatureWithCatalogs(szFileName, hashs.sha1, hCatalog, data)))
                return hr;
            return S_OK;
        }
    }

    if (hashs.md5.GetCount())
    {
        if (FAILED(hr = FindCatalogForHash(hashs.md5, bIsCatalogSigned, hCatalog)))
        {
            log::Verbose(_L_, L"Could not find a catalog for MD5 hash\r\n");
        }
        else if (bIsCatalogSigned)
        {
            // Only if file is catalog signed and hash was passed, proceed with verification
            if (FAILED(hr = VerifySignatureWithCatalogs(szFileName, hashs.md5, hCatalog, data)))
                return hr;
            return S_OK;
        }
    }

    data.bSignatureVerifies = false;
    data.isSigned = false;
    data.AuthStatus = AUTHENTICODE_NOT_SIGNED;
    return S_OK;
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
        return hr = HRESULT_FROM_WIN32(GetLastError());
    }
    BOOST_SCOPE_EXIT((&hMsg)) { CryptMsgClose(hMsg); }
    BOOST_SCOPE_EXIT_END;

    if (dwContentType == CERT_QUERY_CONTENT_PKCS7_SIGNED && dwFormatType == CERT_QUERY_FORMAT_BINARY)
    {
        // Expected message type: PKCS7_SIGNED in binary format
        DWORD dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_CONTENT_PARAM, 0, NULL, &dwBytes))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to query authenticode content info\r\n");
            return hr;
        }
        CBinaryBuffer msgContent;
        msgContent.SetCount(dwBytes);

        if (!CryptMsgGetParam(hMsg, CMSG_CONTENT_PARAM, 0, msgContent.GetData(), &dwBytes))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to get authenticode content info\r\n");
            return hr;
        }

        dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, NULL, &dwBytes))
        {
            log::Error(
                _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to query authenticode content type info\r\n");
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
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to decode authenticode SPC_INDIRECT_DATA info length\r\n");
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
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to decode authenticode SPC_INDIRECT_DATA info\r\n");
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
        return hr = HRESULT_FROM_WIN32(GetLastError());
    }
    BOOST_SCOPE_EXIT((&hMsg)) { CryptMsgClose(hMsg); }
    BOOST_SCOPE_EXIT_END;

    if (dwContentType == CERT_QUERY_CONTENT_PKCS7_SIGNED && dwFormatType == CERT_QUERY_FORMAT_BINARY)
    {
        // Expected message type: PKCS7_SIGNED in binary format
        DWORD dwBytes = 0L;
        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwBytes))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to query authenticode content info\r\n");
            return hr;
        }
        CBinaryBuffer msgContent;
        msgContent.SetCount(dwBytes);

        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, msgContent.GetData(), &dwBytes))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to get authenticode content info\r\n");
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
                    log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptDecodeObject failed\n");
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
                    log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptDecodeObject failed\n");
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
                            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptDecodeObject failed\n");
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

HRESULT Authenticode::ExtractNestedSignature(HCRYPTMSG hMsg, DWORD dwSignerIndex, std::vector<PCCERT_CONTEXT>& pSigners)
{
    HRESULT hr = E_FAIL;
    DWORD cbNeeded = 0L;

    if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, dwSignerIndex, NULL, &cbNeeded))
    {
        if (GetLastError() == CRYPT_E_ATTRIBUTES_MISSING)  // There is no nested signature information to retrieve
            return S_OK;

        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to retrieve nested signature size\r\n");
        return hr;
    }

    CBinaryBuffer crypt_attributes;
    crypt_attributes.SetCount(cbNeeded);

    if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, dwSignerIndex, crypt_attributes.GetData(), &cbNeeded))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to retrieve nested signature\r\n");
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
            log::Debug(_L_, L"Found one nested signature!!!\r\n");

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
                return hr = HRESULT_FROM_WIN32(GetLastError());
            }
            BOOST_SCOPE_EXIT((&hMsg)) { CryptMsgClose(hMsg); }
            BOOST_SCOPE_EXIT_END;

            PCCERT_CONTEXT pSigner = NULL;
            DWORD dwSignerIndex = 0;

            while (CryptMsgGetAndVerifySigner(
                hMsg, 1, &hCertStore, CMSG_USE_SIGNER_INDEX_FLAG | CMSG_SIGNER_ONLY_FLAG, &pSigner, &dwSignerIndex))
            {
                pSigners.push_back(CertDuplicateCertificateContext(pSigner));
                CertFreeCertificateContext(pSigner);

                ExtractNestedSignature(hMsg, dwSignerIndex, pSigners);

                dwSignerIndex++;
            }
            if (GetLastError() != CRYPT_E_INVALID_INDEX)
            {
                log::Error(
                    _L_,
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to extract signer information from blob\r\n");
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
    std::vector<PCCERT_CONTEXT>& pCAs)
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
        return hr = HRESULT_FROM_WIN32(GetLastError());
    }
    BOOST_SCOPE_EXIT((&hMsg)) { CryptMsgClose(hMsg); }
    BOOST_SCOPE_EXIT_END;

    PCCERT_CONTEXT pSigner = NULL;
    DWORD dwSignerIndex = 0;

    while (CryptMsgGetAndVerifySigner(
        hMsg, 1, &hCertStore, CMSG_USE_SIGNER_INDEX_FLAG | CMSG_SIGNER_ONLY_FLAG, &pSigner, &dwSignerIndex))
    {
        pSigners.push_back(CertDuplicateCertificateContext(pSigner));
        CertFreeCertificateContext(pSigner);

        ExtractNestedSignature(hMsg, dwSignerIndex, pSigners);

        dwSignerIndex++;
    }
    if (GetLastError() != CRYPT_E_INVALID_INDEX)
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to extract signer information from blob\r\n");
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
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to obtain certificate chain\r\n");
            break;
        }
        BOOST_SCOPE_EXIT(pChain) { CertFreeCertificateChain(pChain); }
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

HRESULT Authenticode::ExtractCatalogSigners(
    LPCWSTR szCatalogFile,
    std::vector<PCCERT_CONTEXT>& pSigners,
    std::vector<PCCERT_CONTEXT>& pCAs)
{
    HRESULT hr = E_FAIL;

    DWORD dwEncoding, dwContentType, dwFormatType;
    HCERTSTORE hCertStore = NULL;
    HCRYPTMSG hMsg = NULL;

    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_FILE,
            szCatalogFile,
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
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to extract signature information from catalog %s\r\n",
            szCatalogFile);
        return hr;
    }
    BOOST_SCOPE_EXIT((&hMsg)) { CryptMsgClose(hMsg); }
    BOOST_SCOPE_EXIT_END;

    PCCERT_CONTEXT pSigner = NULL;
    DWORD dwSignerIndex = 0;

    while (CryptMsgGetAndVerifySigner(hMsg, 1, &hCertStore, CMSG_USE_SIGNER_INDEX_FLAG, &pSigner, &dwSignerIndex))
    {
        pSigners.push_back(CertDuplicateCertificateContext(pSigner));
        ExtractNestedSignature(hMsg, dwSignerIndex, pSigners);
        dwSignerIndex++;
    }
    if (GetLastError() != CRYPT_E_INVALID_INDEX)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to extract signer information from catalog %s\r\n",
            szCatalogFile);
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
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to obtain certificate chain\r\n");
            return hr;
        }
        BOOST_SCOPE_EXIT(pChain) { CertFreeCertificateChain(pChain); }
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
                log::Error(
                    _L_, hr, L"Failed to extract hash from signature in the security directory of %s\r\n", szFileName);
            }
            if (hashs.md5.GetCount() == data.SignedHashs.md5.GetCount())
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
            else if (hashs.sha1.GetCount() == data.SignedHashs.sha1.GetCount())
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
            else if (hashs.sha256.GetCount() == data.SignedHashs.sha256.GetCount())
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
                log::Warning(
                    _L_,
                    E_UNEXPECTED,
                    L"None of the available hashs could be used to compare to the hash in the signature: signature "
                    L"could not be verified\r\n");
                data.bSignatureVerifies = false;
            }

            FILETIME timestamp = {0L, 0L};

            if (FAILED(hr = ExtractSignatureTimeStamp(signature, data)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to extract timestamp information from signature in the security directory of %s\r\n",
                    szFileName);
            }

            if (FAILED(hr = ExtractSignatureSigners(signature, data.Timestamp, data.Signers, data.SignersCAs)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to extract signer information from signature in the security directory of %s\r\n",
                    szFileName);
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

HRESULT Authenticode::CloseCatalogState()
{
    WINTRUST_DATA sWTD;
    WINTRUST_CATALOG_INFO sWTCI;

    memset(&sWTD, 0x00, sizeof(WINTRUST_DATA));
    sWTD.cbStruct = sizeof(WINTRUST_DATA);
    sWTD.dwUIChoice = WTD_UI_NONE;
    sWTD.dwUnionChoice = WTD_CHOICE_CATALOG;
    sWTD.pCatalog = &sWTCI;
    sWTD.dwStateAction = WTD_STATEACTION_CLOSE;

    memset(&sWTCI, 0x00, sizeof(WINTRUST_CATALOG_INFO));
    sWTCI.cbStruct = sizeof(WINTRUST_CATALOG_INFO);

    std::for_each(
        m_StateMap.begin(), m_StateMap.end(), [&sWTD, this](const std::pair<const std::wstring&, const HANDLE> pair) {
            if (pair.second)
            {
                sWTD.hWVTStateData = pair.second;

                m_wintrust.WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &WVTPolicyGUID, &sWTD);
            }
        });
    m_StateMap.clear();
    return S_OK;
}

Authenticode::~Authenticode()
{
    CloseCatalogState();
    if (m_hContext != INVALID_HANDLE_VALUE)
        m_wintrust.CryptCATAdminReleaseContext(m_hContext, 0);
    if (m_hMachineStore != INVALID_HANDLE_VALUE)
        CertCloseStore(m_hMachineStore, 0L);
}
