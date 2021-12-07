//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once
#include "MessageStream.h"

#pragma managed(push, off)

namespace Orc {

class DecodeMessageStream : public MessageStream
{
private:
    CMSG_CTRL_DECRYPT_PARA m_DecryptParam;
    HCERTSTORE m_hSystemStore = NULL;

    bool m_bDecrypting = false;

    std::vector<PCCERT_CONTEXT> m_Recipients;

    HRESULT
    GetCertPrivateKey(DWORD cbRecipientIndex, PCCERT_CONTEXT pCertContext, CMSG_CTRL_DECRYPT_PARA& DecryptParam);

    HRESULT GetDecryptionMaterial();

public:
    DecodeMessageStream()
        : MessageStream()
    {
        ZeroMemory(&m_DecryptParam, sizeof(CMSG_CTRL_DECRYPT_PARA));
        m_DecryptParam.cbSize = sizeof(CMSG_CTRL_DECRYPT_PARA);
    };

    STDMETHOD(Initialize)(const std::shared_ptr<ByteStream>& pInnerStream);

    HRESULT GetRecipients();
    PCCERT_CONTEXT GetDecryptor() const
    {
        if (m_DecryptParam.hCryptProv != NULL)
            return m_Recipients[m_DecryptParam.dwRecipientIndex];
        return nullptr;
    };

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

    ~DecodeMessageStream();
};

}  // namespace Orc

#pragma managed(pop)
