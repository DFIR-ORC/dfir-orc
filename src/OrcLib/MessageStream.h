//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once
#include "ChainingStream.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API MessageStream : public ChainingStream
{
protected:
    CMSG_STREAM_INFO m_StreamInfo;

    HCRYPTPROV m_hProv = NULL;

    HCERTSTORE m_hCertStore = NULL;

    HCRYPTMSG m_hMsg = NULL;

    STDMETHOD(OpenCertStore)();

public:
    MessageStream(logger pLog)
        : ChainingStream(std::move(pLog))
    {
        ZeroMemory(&m_StreamInfo, sizeof(CMSG_STREAM_INFO));
    };

    static HRESULT CertNameToString(_In_ PCERT_NAME_BLOB pName, _In_ DWORD dwStrType, std::wstring& strName);
    static HRESULT CertSerialNumberToString(_In_ PCRYPT_INTEGER_BLOB pSerial, std::wstring& strSerialNumber);

    STDMETHOD(AddCertificate)(const CBinaryBuffer& buffer, PCCERT_CONTEXT& pCertContext);
    STDMETHOD(AddCertificate)
    (LPCWSTR szEncodedCert, DWORD cchLen, PCCERT_CONTEXT& pCertContext, DWORD dwFlags = CRYPT_STRING_ANY);
    STDMETHOD(AddCertificate)
    (LPCSTR szEncodedCert, DWORD cchLen, PCCERT_CONTEXT& pCertContext, DWORD dwFlags = CRYPT_STRING_ANY);
    STDMETHOD(AddCertificate)
    (const std::wstring& strEncodedCert, PCCERT_CONTEXT& pCertContext, DWORD dwFlags = CRYPT_STRING_ANY)
    {
        return AddCertificate(strEncodedCert.c_str(), (DWORD)strEncodedCert.size(), pCertContext, dwFlags);
    }
    STDMETHOD(AddCertificate)
    (const std::string& strEncodedCert, PCCERT_CONTEXT& pCertContext, DWORD dwFlags = CRYPT_STRING_ANY)
    {
        return AddCertificate(strEncodedCert.c_str(), (DWORD)strEncodedCert.size(), pCertContext, dwFlags);
    }

    STDMETHOD(Initialize)(const std::shared_ptr<ByteStream>& pInnerStream) PURE;

    ~MessageStream();
};

}  // namespace Orc

#pragma managed(pop)
