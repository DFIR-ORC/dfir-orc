//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "DecodeMessageStream.h"
#include "LogFileWriter.h"

#include "boost/scope_exit.hpp"

using namespace Orc;

STDMETHODIMP DecodeMessageStream::Initialize(const std::shared_ptr<ByteStream>& pInnerStream)
{
    HRESULT hr = E_FAIL;

    m_pChainedStream = pInnerStream;

    ZeroMemory(&m_StreamInfo, sizeof(CMSG_STREAM_INFO));

    m_StreamInfo.cbContent = CMSG_INDEFINITE_LENGTH;
    m_StreamInfo.pvArg = m_pChainedStream.get();
    m_StreamInfo.pfnStreamOutput =
        (PFN_CMSG_STREAM_OUTPUT)[](IN const void* pvArg, IN BYTE* pbData, IN DWORD cbData, IN BOOL fFinal)->BOOL
    {

        auto pStream = (ByteStream*)pvArg;
        ULONGLONG ullWritten = 0LL;

        if (pStream == nullptr)
            return FALSE;

        if (FAILED(pStream->Write(pbData, cbData, &ullWritten)))
            return FALSE;

        if (fFinal)
            if (FAILED(pStream->Close()))
                return FALSE;

        return TRUE;
    };

    m_hMsg = CryptMsgOpenToDecode(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, CMSG_CRYPT_RELEASE_CONTEXT_FLAG, 0L, NULL, NULL, &m_StreamInfo);

    if (FAILED(hr = m_pChainedStream->SetFilePointer(0, SEEK_SET, NULL)))
        return hr;

    m_hSystemStore = CertOpenSystemStore(NULL, L"MY");
    if (m_hSystemStore == NULL)
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CertOpenSystemStore failed!!!\r\n");
        return hr;
    }

    return S_OK;
}

__data_entrypoint(File) HRESULT DecodeMessageStream::Read(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);

    *pcbBytesRead = 0;

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;

    log::Verbose(_L_, L"Cannot read from an EncodeMessageStream\r\n");
    return E_NOTIMPL;
}

HRESULT DecodeMessageStream::GetCertPrivateKey(
    DWORD dwRecipientIndex,
    PCCERT_CONTEXT pCertContext,
    CMSG_CTRL_DECRYPT_PARA& DecryptParam)
{
    HRESULT hr = E_FAIL;
    PCRYPT_KEY_PROV_INFO pProvInfo = nullptr;
    DWORD cbProvInfo = 0L;

    if (!CertGetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, pProvInfo, &cbProvInfo))
    {
        if (GetLastError() != ERROR_MORE_DATA)
        {
            log::Error(
                _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CertGetCertificateContextProperty failed!!!\r\n");
            return hr;
        }
    }

    CBinaryBuffer ProvBuffer;
    ProvBuffer.SetCount(cbProvInfo);
    pProvInfo = (PCRYPT_KEY_PROV_INFO)ProvBuffer.GetData();

    if (!CertGetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, pProvInfo, &cbProvInfo))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CertGetCertificateContextProperty failed!!!\r\n");
        return hr;
    }

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE hKeyHandle = NULL;
    DWORD dwKeySpec = 0L;
    BOOL fCallerFreeProvOrNCryptKey = FALSE;

    if (!CryptAcquireCertificatePrivateKey(
            pCertContext,
            CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_USE_PROV_INFO_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG,
            NULL,
            &hKeyHandle,
            &dwKeySpec,
            &fCallerFreeProvOrNCryptKey))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptAcquireCertificatePrivateKey failed!!!\r\n");
        return hr;
    }

    ZeroMemory(&DecryptParam, sizeof(CMSG_CTRL_DECRYPT_PARA));
    DecryptParam.cbSize = sizeof(CMSG_CTRL_DECRYPT_PARA);

    DecryptParam.dwKeySpec = dwKeySpec;
    DecryptParam.hCryptProv = hKeyHandle;
    DecryptParam.dwRecipientIndex = dwRecipientIndex;

    return S_OK;
}

HRESULT DecodeMessageStream::GetRecipients()
{
    HRESULT hr = E_FAIL;
    DWORD dwRecipientCount = 0L;
    DWORD cbRecipientCount = sizeof(DWORD);

    if (!CryptMsgGetParam(m_hMsg, CMSG_RECIPIENT_COUNT_PARAM, 0L, &dwRecipientCount, &cbRecipientCount))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgGetParam failed!!!\r\n");
    }
    else
    {

        for (unsigned int i = 0; i < dwRecipientCount; i++)
        {
            PCERT_INFO pRecipientInfo = nullptr;
            DWORD cbRecipientInfo = 0L;
            if (!CryptMsgGetParam(m_hMsg, CMSG_RECIPIENT_INFO_PARAM, i, pRecipientInfo, &cbRecipientInfo))
            {
                if (GetLastError() != ERROR_MORE_DATA)
                {
                    log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgGetParam failed!!!\r\n");
                    return hr;
                }
            }

            CBinaryBuffer RecipientBuffer;
            RecipientBuffer.SetCount(cbRecipientInfo);
            RecipientBuffer.ZeroMe();

            pRecipientInfo = (PCERT_INFO)RecipientBuffer.GetData();

            if (!CryptMsgGetParam(m_hMsg, CMSG_RECIPIENT_INFO_PARAM, i, pRecipientInfo, &cbRecipientInfo))
            {
                log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgGetParam failed!!!\r\n");
            }
            else
            {
                PCCERT_CONTEXT pCertContext = CertGetSubjectCertificateFromStore(
                    m_hSystemStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pRecipientInfo);
                BOOST_SCOPE_EXIT((&pCertContext)) { CertFreeCertificateContext(pCertContext); }
                BOOST_SCOPE_EXIT_END;

                if (pCertContext == NULL)
                {
                    std::wstring strIssuer;
                    CertNameToString(&pRecipientInfo->Issuer, CERT_SIMPLE_NAME_STR, strIssuer);

                    std::wstring strSerial;
                    CertSerialNumberToString(&pRecipientInfo->SerialNumber, strSerial);

                    log::Verbose(
                        _L_, L"Unknon certificate (Issuer=\"%s\" SN=%s\r\n", strIssuer.c_str(), strSerial.c_str());
                }
                else
                {
                    PCCERT_CONTEXT pMyCertContext = NULL;
                    if (!CertAddCertificateContextToStore(
                            m_hCertStore, pCertContext, CERT_STORE_ADD_NEW, &pMyCertContext))
                    {
                        log::Error(
                            _L_,
                            hr = HRESULT_FROM_WIN32(GetLastError()),
                            L"CertAddCertificateContextToStore failed!!!\r\n");
                        return hr;
                    }
                    m_Recipients.push_back(pMyCertContext);
                }
            }
        }
    }
    return S_OK;
}

HRESULT DecodeMessageStream::GetDecryptionMaterial()
{
    HRESULT hr = E_FAIL;

    if (m_bDecrypting)
        return S_OK;

    // We do not have decrypting material yet
    PCRYPT_ALGORITHM_IDENTIFIER pAlgId = nullptr;
    DWORD cbAlgId = 0L;

    if (!CryptMsgGetParam(m_hMsg, CMSG_ENVELOPE_ALGORITHM_PARAM, 0L, NULL, &cbAlgId))
    {
        if (GetLastError() == CRYPT_E_STREAM_MSG_NOT_READY)
        {
            log::Verbose(_L_, L"CryptMsgGetParam says that encryption algo is not yet available, need to continue\r\n");
            return S_OK;
        }
        else if (GetLastError() != ERROR_MORE_DATA)
        {
            log::Error(
                _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgGetParam failed for unexpected reason!!!\r\n");
            return hr;
        }
    }

    CBinaryBuffer AlgBuffer;
    AlgBuffer.SetCount(cbAlgId);

    pAlgId = (PCRYPT_ALGORITHM_IDENTIFIER)AlgBuffer.GetData();
    if (!CryptMsgGetParam(m_hMsg, CMSG_ENVELOPE_ALGORITHM_PARAM, 0L, pAlgId, &cbAlgId))
    {
        log::Error(
            _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgGetParam failed for unexpected reason!!!\r\n");
        return hr;
    }
    log::Verbose(_L_, L"CryptMsgGetParam says that encryption information is now available\r\n");

    if (FAILED(GetRecipients()))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to determine the list of recipients!!!\r\n");
        return hr;
    }

    DWORD idx = 0L;

    for (auto pCert : m_Recipients)
    {

        CMSG_CTRL_DECRYPT_PARA params;
        ZeroMemory(&params, sizeof(CMSG_CTRL_DECRYPT_PARA));
        params.cbSize = sizeof(CMSG_CTRL_DECRYPT_PARA);

        if (SUCCEEDED(hr = GetCertPrivateKey(idx, pCert, params)))
        {
            if (!CryptMsgControl(m_hMsg, CMSG_CRYPT_RELEASE_CONTEXT_FLAG, CMSG_CTRL_DECRYPT, &params))
            {
                log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgControl failed!!!\r\n");
                return hr;
            }
            else
            {
                m_DecryptParam = params;
                log::Verbose(_L_, L"Decryption information is now available\r\n");
                m_bDecrypting = true;
                break;
            }
        }
        idx++;
    }

    return S_OK;
}

HRESULT DecodeMessageStream::Write(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (m_hMsg == NULL)
        return E_POINTER;

    if (cbBytes > MAXDWORD)
    {
        log::Error(_L_, E_INVALIDARG, L"Too many bytes to decode\r\n");
        return E_INVALIDARG;
    }

    if (!CryptMsgUpdate(m_hMsg, (const BYTE*)pBuffer, static_cast<DWORD>(cbBytes), FALSE))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgUpdate failed\r\n");
        return hr;
    }

    if (!m_bDecrypting)
    {
        if (FAILED(hr = GetDecryptionMaterial()))
            return hr;
    }

    *pcbBytesWritten = cbBytes;
    log::Verbose(_L_, L"CryptMsgUpdate %d bytes succeeded (hMsg=0x%lx)\r\n", cbBytes, m_hMsg);
    return S_OK;
}

HRESULT DecodeMessageStream::SetFilePointer(
    __in LONGLONG lDistanceToMove,
    __in DWORD dwMoveMethod,
    __out_opt PULONG64 pqwCurrPointer)
{
    DBG_UNREFERENCED_PARAMETER(lDistanceToMove);
    DBG_UNREFERENCED_PARAMETER(dwMoveMethod);
    DBG_UNREFERENCED_PARAMETER(pqwCurrPointer);
    log::Verbose(_L_, L"SetFilePointer is not implemented in DecodeMessageStream\r\n");
    return S_OK;
}

ULONG64 DecodeMessageStream::GetSize()
{
    log::Verbose(_L_, L"GetFileSizeEx is not implemented on DecodeMessageStream\r\n");
    return (ULONG64)-1;
}

HRESULT DecodeMessageStream::SetSize(ULONG64 ullNewSize)
{
    DBG_UNREFERENCED_PARAMETER(ullNewSize);
    log::Verbose(_L_, L"SetSize is not implemented on DecodeMessageStream\r\n");
    return S_OK;
}

HRESULT DecodeMessageStream::Close()
{
    if (m_hMsg != NULL)
    {
        HRESULT hr = E_FAIL;
        if (!CryptMsgUpdate(m_hMsg, NULL, 0L, TRUE))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Final CryptMsgUpdate failed\r\n");
            return hr;
        }
        if (!CryptMsgClose(m_hMsg))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CryptMsgClose failed\r\n");
            m_hMsg = NULL;
            return hr;
        }
        m_hMsg = NULL;
    }
    return S_OK;
}

DecodeMessageStream::~DecodeMessageStream()
{
    for (auto pCert : m_Recipients)
    {
        CertFreeCertificateContext(pCert);
    }
    if (m_hCertStore)
    {
        CertCloseStore(m_hSystemStore, 0L);
        m_hCertStore = NULL;
    }
}
