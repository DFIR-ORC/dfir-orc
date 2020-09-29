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

class TemporaryStream;

class ORCLIB_API AccumulatingStream : public ChainingStream
{
private:
    std::shared_ptr<TemporaryStream> m_pTempStream;
    bool m_bClosed = false;
    DWORD m_dwBlockSize = 0;

public:
    AccumulatingStream()
        : ChainingStream() {};

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {
        if (m_pChainedStream)
        {
            return m_pChainedStream->IsOpen();
        }
        return S_FALSE;
    };
    STDMETHOD(CanRead)() { return S_FALSE; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };
    //
    // CByteStream implementation
    //
    STDMETHOD(Open)
    (const std::shared_ptr<ByteStream>& pChainedStream,
     const std::wstring& strTempDir,
     DWORD dwMemThreshold,
     DWORD dwBlockSize = DEFAULT_READ_SIZE);

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

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);

    STDMETHOD(Close)();

    virtual ~AccumulatingStream();
};
}  // namespace Orc

#pragma managed(pop)
