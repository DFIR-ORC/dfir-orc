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

class HashStream : public ChainingStream
{
protected:
    bool m_bWriteOnly;
    bool m_bHashIsValid;

    STDMETHOD(ResetHash(bool bContinue = false)) PURE;
    STDMETHOD(HashData(LPBYTE pBuffer, DWORD dwBytesToHash)) PURE;

public:
    HashStream()
        : ChainingStream()
        , m_bWriteOnly(false)
        , m_bHashIsValid(false) {};

    STDMETHOD(IsOpen)()
    {
        if (m_bWriteOnly)
            return S_OK;
        return ChainingStream::IsOpen();
    };
    STDMETHOD(CanRead)()
    {
        if (m_bWriteOnly)
            return S_FALSE;
        return ChainingStream::CanRead();
    };
    STDMETHOD(CanWrite)()
    {
        if (m_bWriteOnly)
            return S_OK;
        return ChainingStream::CanWrite();
    };
    STDMETHOD(CanSeek)()
    {
        if (m_bWriteOnly)
            return S_OK;
        return ChainingStream::CanSeek();
    };

    HRESULT HasValidHash() { return m_bHashIsValid ? S_OK : S_FALSE; };

    //
    // CByteStream implementation
    //

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)()
    {
        if (m_pChainedStream == nullptr)
            return 0LL;
        return m_pChainedStream->GetSize();
    }
    STDMETHOD(SetSize)(ULONG64 ullSize)
    {
        if (m_pChainedStream == nullptr)
            return S_OK;
        return m_pChainedStream->SetSize(ullSize);
    }

    STDMETHOD(Close)();

    virtual ~HashStream();
};

}  // namespace Orc

#pragma managed(pop)
