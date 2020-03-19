//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ChainingStream.h"

#include <memory>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API XORStream : public ChainingStream
{
protected:
    DWORD m_OriginalXORPattern;
    DWORD m_CurrentXORPattern;

public:
    XORStream(logger pLog)
        : ChainingStream(std::move(pLog))
        , m_OriginalXORPattern(0L)
        , m_CurrentXORPattern(0L) {};
    ~XORStream(void);

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {
        if (m_pChainedStream == NULL)
            return S_FALSE;
        return m_pChainedStream->IsOpen();
    };
    STDMETHOD(CanRead)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanRead();
    };
    STDMETHOD(CanWrite)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanWrite();
    };
    STDMETHOD(CanSeek)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanSeek();
    };

    //
    // CByteStream implementation
    //
    STDMETHOD(OpenForXOR)(const std::shared_ptr<ByteStream>& pChainedStream);

    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
    {
        if (m_pChainedStream == nullptr)
            return E_POINTER;
        return m_pChainedStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
    }

    STDMETHOD_(ULONG64, GetSize)()
    {
        if (m_pChainedStream == nullptr)
            return 0;
        return m_pChainedStream->GetSize();
    }
    STDMETHOD(SetSize)(ULONG64 ullNewSize)
    {
        if (m_pChainedStream == nullptr)
            return S_OK;
        return m_pChainedStream->SetSize(ullNewSize);
    }

    STDMETHOD(Close)()
    {
        if (m_pChainedStream == nullptr)
            return S_OK;
        return m_pChainedStream->Close();
    }

    // XORStream

    STDMETHOD(XORPrefixFileName)(const WCHAR* szFileName, WCHAR* szPrefixedFileName, DWORD dwPrefixedNameLen);

    static HRESULT IsNameXORPrefixed(const WCHAR* szFileName);

    STDMETHOD(GetXORPatternFromName)(const WCHAR* szFileName, DWORD& dwXORPattern);
    STDMETHOD(XORUnPrefixFileName)(const WCHAR* szFileName, WCHAR* szPrefixedFileName, DWORD dwPrefixedNameLen);

    STDMETHOD(SetXORPattern)(DWORD dwPattern);
    STDMETHOD(XORMemory)(LPBYTE pDest, DWORD cbDestBytes, LPBYTE pSrc, DWORD cbSrcBytes, LPDWORD pdwXORed);
};

}  // namespace Orc

#pragma managed(pop)
