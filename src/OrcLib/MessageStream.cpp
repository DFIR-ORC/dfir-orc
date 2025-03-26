//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "MessageStream.h"
#include "BinaryBuffer.h"

using namespace Orc;

HRESULT MessageStream::CertNameToString(_In_ PCERT_NAME_BLOB pName, _In_ DWORD dwStrType, std::wstring& strName)
{
    DWORD dwNeededChars = CertNameToStr(X509_ASN_ENCODING, pName, dwStrType, NULL, 0L);

    if (dwNeededChars > 0)
    {
        CBinaryBuffer buffer;
        buffer.SetCount(msl::utilities::SafeInt<USHORT>(dwNeededChars) * sizeof(WCHAR));
        buffer.ZeroMe();
        DWORD dwConvertedChars =
            CertNameToStr(X509_ASN_ENCODING, pName, dwStrType, buffer.GetP<WCHAR>(0), dwNeededChars);
        strName.assign(buffer.GetP<WCHAR>(0), dwConvertedChars);
    }
    return S_OK;
}

HRESULT MessageStream::CertSerialNumberToString(_In_ PCRYPT_INTEGER_BLOB pSerial, std::wstring& strSerialNumber)
{
    CBinaryBuffer buffer;
    if (!buffer.SetCount(pSerial->cbData))
        return E_OUTOFMEMORY;

    INT j = 0;
    for (INT i = pSerial->cbData - 1; i >= 0; i--)
    {
        buffer.Get<BYTE>(j) = pSerial->pbData[i];
        j++;
    }

    WCHAR szSerialNumber[ORC_MAX_PATH];
    DWORD dwCharCount = ORC_MAX_PATH;
    CryptBinaryToString(
        buffer.GetData(),
        (DWORD)buffer.GetCount(),
        CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF,
        szSerialNumber,
        &dwCharCount);

    strSerialNumber.assign(szSerialNumber, dwCharCount);
    return S_OK;
}

STDMETHODIMP MessageStream::OpenCertStore()
{
    HRESULT hr = E_FAIL;
    m_hCertStore = CertOpenStore(sz_CERT_STORE_PROV_MEMORY, 0L, m_hProv, CERT_STORE_CREATE_NEW_FLAG, NULL);

    if (m_hCertStore == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CertOpenStore [{}]", SystemError(hr));
        return hr;
    }
    return S_OK;
}

STDMETHODIMP MessageStream::AddCertificate(const CBinaryBuffer& buffer, PCCERT_CONTEXT& pCertContext)
{
    HRESULT hr = E_FAIL;
    if (!CertAddEncodedCertificateToStore(
            m_hCertStore,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            buffer.GetData(),
            (DWORD)buffer.GetCount(),
            CERT_STORE_ADD_NEW,
            &pCertContext))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed CertAddEncodedCertificateToStore [{}]", SystemError(hr));
        return hr;
    }
    return S_OK;
}

STDMETHODIMP
MessageStream::AddCertificate(LPCWSTR szEncodedCert, DWORD cchLen, PCCERT_CONTEXT& pCertContext, DWORD dwFlags)
{
    HRESULT hr = E_FAIL;
    DWORD cbOutput = 0L;
    DWORD dwFormatFlags = 0L;

    if (!CryptStringToBinaryW(szEncodedCert, cchLen, dwFlags, NULL, &cbOutput, NULL, &dwFormatFlags))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(
            L"Failed CryptStringToBinaryW: cannot convert '{}' into a binary blob [{}]",
            szEncodedCert,
            SystemError(hr));
        return hr;
    }

    CBinaryBuffer pCertBin;
    pCertBin.SetCount(cbOutput);
    if (!CryptStringToBinaryW(szEncodedCert, cchLen, dwFlags, pCertBin.GetData(), &cbOutput, NULL, &dwFormatFlags))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(
            L"Failed CryptStringToBinaryW: cannot convert '{}' into a binary blob [{}]",
            szEncodedCert,
            SystemError(hr));
        return hr;
    }
    return AddCertificate(pCertBin, pCertContext);
}

STDMETHODIMP
MessageStream::AddCertificate(LPCSTR szEncodedCert, DWORD cchLen, PCCERT_CONTEXT& pCertContext, DWORD dwFlags)
{
    HRESULT hr = E_FAIL;
    DWORD cbOutput = 0L;
    DWORD dwFormatFlags = 0L;

    if (!CryptStringToBinaryA(szEncodedCert, cchLen, dwFlags, NULL, &cbOutput, NULL, &dwFormatFlags))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(
            "Failed CryptStringToBinaryA: cannot convert '{}' into a binary blob [{}]", szEncodedCert, SystemError(hr));
        return hr;
    }

    CBinaryBuffer pCertBin;
    pCertBin.SetCount(cbOutput);
    if (!CryptStringToBinaryA(szEncodedCert, cchLen, dwFlags, pCertBin.GetData(), &cbOutput, NULL, &dwFormatFlags))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(
            "Failed CryptStringToBinaryA: cannot convert '{}' into a binary blob [{}]", szEncodedCert, SystemError(hr));
        return hr;
    }
    return AddCertificate(pCertBin, pCertContext);
}

MessageStream::~MessageStream()
{
    if (m_hProv != NULL)
    {
        CryptReleaseContext(m_hProv, 0L);
        m_hProv = NULL;
    }
}
