//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "MessageStream.h"

#include <boost/logic/tribool.hpp>

#pragma managed(push, off)

namespace Orc {

class EncodeMessageStream : public MessageStream
{
protected:
    HCRYPTPROV m_hProv = NULL;

    std::vector<PCCERT_CONTEXT> m_recipients;

    CMSG_ENVELOPED_ENCODE_INFO EncodeInfo;

public:
    EncodeMessageStream()
        : MessageStream()
    {
        ZeroMemory(&EncodeInfo, sizeof(CMSG_ENVELOPED_ENCODE_INFO));
    }

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(AddRecipient)(const CBinaryBuffer& buffer);
    STDMETHOD(AddRecipient)(LPCSTR szEncodedCert, DWORD cchLen, DWORD dwFlags = CRYPT_STRING_ANY);
    STDMETHOD(AddRecipient)(LPCWSTR szEncodedCert, DWORD cchLen, DWORD dwFlags = CRYPT_STRING_ANY);
    STDMETHOD(AddRecipient)(const std::string& strEncodedCert, DWORD dwFlags = CRYPT_STRING_ANY);
    STDMETHOD(AddRecipient)(const std::wstring& strEncodedCert, DWORD dwFlags = CRYPT_STRING_ANY);

    STDMETHOD(Initialize)(const std::shared_ptr<ByteStream>& pInnerStream);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytesToRead,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);

    STDMETHOD(Close)();

    ~EncodeMessageStream();
};
}  // namespace Orc

#pragma managed(pop)
