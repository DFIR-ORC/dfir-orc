//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ChainingStream.h"

#include <boost/logic/tribool.hpp>

#pragma managed(push, off)

namespace Orc {

class PasswordEncryptedStream : public ChainingStream
{
private:
    HCRYPTPROV m_hCryptProv = NULL;
    HCRYPTKEY m_hKey = NULL;
    DWORD m_dwBlockLen = 0L;
    boost::logic::tribool m_bEncrypting;

    CBinaryBuffer m_Buffer;
    DWORD m_dwBufferedData = 0L;

    HRESULT GetKeyMaterial(const std::wstring& pwd);

    HRESULT EncryptData(CBinaryBuffer& pData, BOOL bFinal, DWORD& dwEncryptedBytes);
    HRESULT DecryptData(CBinaryBuffer& pData, BOOL bFinal, DWORD& dwDecryptedBytes);

public:
    PasswordEncryptedStream()
        : ChainingStream()
    {
        m_bEncrypting = boost::indeterminate;
    }

    STDMETHOD(IsOpen)()
    {
        if (m_bEncrypting == boost::indeterminate)
            return S_FALSE;
        return ChainingStream::IsOpen();
    };
    STDMETHOD(CanRead)()
    {
        if (m_bEncrypting)
            return S_FALSE;
        return ChainingStream::CanRead();
    };
    STDMETHOD(CanWrite)()
    {
        if (m_bEncrypting)
            return S_OK;
        return ChainingStream::CanWrite();
    };
    STDMETHOD(CanSeek)() { return S_FALSE; };

    STDMETHOD(OpenToEncrypt)(const std::wstring& pwd, const std::shared_ptr<ByteStream>& pChainedStream);
    STDMETHOD(OpenToDecrypt)(const std::wstring& pwd, const std::shared_ptr<ByteStream>& pChainedStream);

    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)()
    {
        if (m_pChainedStream == nullptr)
            return 0;
        ULONG64 ullSize = m_pChainedStream->GetSize();
        return ullSize % m_dwBlockLen == 0 ? ullSize : (ullSize / m_dwBlockLen) * (m_dwBlockLen + 1);
    }
    STDMETHOD(SetSize)(ULONG64 ullSize)
    {
        if (m_pChainedStream == nullptr)
            return S_OK;
        return m_pChainedStream->SetSize(
            ullSize % m_dwBlockLen == 0 ? ullSize : (ullSize / m_dwBlockLen) * (m_dwBlockLen + 1));
    }

    STDMETHOD(Close)();

    ~PasswordEncryptedStream();
};

}  // namespace Orc

#pragma managed(pop)
