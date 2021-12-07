//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "DecodeMessageStream.h"
#include "BinaryBuffer.h"

#include "boost/scope_exit.hpp"

using namespace Orc;

namespace {

BOOL CALLBACK StreamOutputCb(IN const void* pvArg, IN BYTE* pbData, IN DWORD cbData, IN BOOL fFinal)
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
}

}  // namespace

STDMETHODIMP DecodeMessageStream::Initialize(const std::shared_ptr<ByteStream>& pInnerStream)
{
    HRESULT hr = E_FAIL;

    m_pChainedStream = pInnerStream;

    ZeroMemory(&m_StreamInfo, sizeof(CMSG_STREAM_INFO));

    m_StreamInfo.cbContent = CMSG_INDEFINITE_LENGTH;
    m_StreamInfo.pvArg = m_pChainedStream.get();
    m_StreamInfo.pfnStreamOutput = ::StreamOutputCb;

    m_hMsg = CryptMsgOpenToDecode(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, CMSG_CRYPT_RELEASE_CONTEXT_FLAG, 0L, NULL, NULL, &m_StreamInfo);

    if (FAILED(hr = m_pChainedStream->SetFilePointer(0, SEEK_SET, NULL)))
        return hr;

    m_hSystemStore = CertOpenSystemStore(NULL, L"MY");
    if (m_hSystemStore == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CertOpenSystemStore [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

__data_entrypoint(File) HRESULT DecodeMessageStream::Read_(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);

    *pcbBytesRead = 0;

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;

    Log::Debug("Cannot read from an EncodeMessageStream");
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
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed CertGetCertificateContextProperty [{}]", SystemError(hr));
            return hr;
        }
    }

    CBinaryBuffer ProvBuffer;
    ProvBuffer.SetCount(cbProvInfo);
    pProvInfo = (PCRYPT_KEY_PROV_INFO)ProvBuffer.GetData();

    if (!CertGetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, pProvInfo, &cbProvInfo))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CertGetCertificateContextProperty [{}]", SystemError(hr));
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
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CryptAcquireCertificatePrivateKey [{}]", SystemError(hr));
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
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CryptMsgGetParam [{}]", SystemError(hr));
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
                    hr = HRESULT_FROM_WIN32(GetLastError());
                    Log::Error("Failed CryptMsgGetParam [{}]", SystemError(hr));
                    return hr;
                }
            }

            CBinaryBuffer RecipientBuffer;
            RecipientBuffer.SetCount(cbRecipientInfo);
            RecipientBuffer.ZeroMe();

            pRecipientInfo = (PCERT_INFO)RecipientBuffer.GetData();

            if (!CryptMsgGetParam(m_hMsg, CMSG_RECIPIENT_INFO_PARAM, i, pRecipientInfo, &cbRecipientInfo))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error("Failed CryptMsgGetParam [{}]", SystemError(hr));
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

                    Log::Debug(L"Unknown certificate (Issuer=\"{}\", SN=\"{}\")", strIssuer, strSerial);
                }
                else
                {
                    PCCERT_CONTEXT pMyCertContext = NULL;
                    if (!CertAddCertificateContextToStore(
                            m_hCertStore, pCertContext, CERT_STORE_ADD_NEW, &pMyCertContext))
                    {
                        hr = HRESULT_FROM_WIN32(GetLastError());
                        Log::Error("Failed CertAddCertificateContextToStore [{}]", SystemError(hr));
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
            Log::Debug("CryptMsgGetParam says that encryption algo is not yet available, need to continue");
            return S_OK;
        }
        else if (GetLastError() != ERROR_MORE_DATA)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed CryptMsgGetParam [{}]", SystemError(hr));
            return hr;
        }
    }

    CBinaryBuffer AlgBuffer;
    AlgBuffer.SetCount(cbAlgId);

    pAlgId = (PCRYPT_ALGORITHM_IDENTIFIER)AlgBuffer.GetData();
    if (!CryptMsgGetParam(m_hMsg, CMSG_ENVELOPE_ALGORITHM_PARAM, 0L, pAlgId, &cbAlgId))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CryptMsgGetParam [{}]", SystemError(hr));
        return hr;
    }
    Log::Debug("CryptMsgGetParam says that encryption information is now available");

    if (FAILED(GetRecipients()))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed to determine the list of recipients [{}]", SystemError(hr));
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
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Error("Failed CryptMsgControl [{}]", SystemError(hr));
                return hr;
            }
            else
            {
                m_DecryptParam = params;
                Log::Debug("Decryption information is now available");
                m_bDecrypting = true;
                break;
            }
        }
        idx++;
    }

    return S_OK;
}

HRESULT DecodeMessageStream::Write_(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (m_hMsg == NULL)
        return E_POINTER;

    if (cbBytes > MAXDWORD)
    {
        Log::Error("DecodeMessageStream: Write: Too many bytes to decode");
        return E_INVALIDARG;
    }

    if (!CryptMsgUpdate(m_hMsg, (const BYTE*)pBuffer, static_cast<DWORD>(cbBytes), FALSE))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CryptMsgUpdate [{}]", SystemError(hr));
        return hr;
    }

    if (!m_bDecrypting)
    {
        if (FAILED(hr = GetDecryptionMaterial()))
            return hr;
    }

    *pcbBytesWritten = cbBytes;
    Log::Debug("CryptMsgUpdate {} bytes succeeded (hMsg: {:p})", cbBytes, m_hMsg);
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
    Log::Debug(L"DecodeMessageStream: SetFilePointer is not implemented");
    return S_OK;
}

ULONG64 DecodeMessageStream::GetSize()
{
    Log::Debug(L"DecodeMessageStream: GetSize is not implemented");
    return (ULONG64)-1;
}

HRESULT DecodeMessageStream::SetSize(ULONG64 ullNewSize)
{
    DBG_UNREFERENCED_PARAMETER(ullNewSize);
    Log::Debug(L"DecodeMessageStream: SetSize is not implemented");
    return S_OK;
}

HRESULT DecodeMessageStream::Close()
{
    if (m_hMsg != NULL)
    {
        HRESULT hr = E_FAIL;
        if (!CryptMsgUpdate(m_hMsg, NULL, 0L, TRUE))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed CryptMsgUpdate on final call [{}]", SystemError(hr));
            return hr;
        }
        if (!CryptMsgClose(m_hMsg))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed CryptMsgClose [{}]", SystemError(hr));
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
