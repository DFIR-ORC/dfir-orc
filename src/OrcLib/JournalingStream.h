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

class LogFileWriter;

class ORCLIB_API JournalingStream : public ChainingStream
{
private:
    ULONGLONG m_ullCurrentPosition = 0LL;
    ULONGLONG m_ullStreamSize = 0LL;
    bool bClosed = false;

public:
    JournalingStream(logger pLog)
        : ChainingStream(std::move(pLog)) {};

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
    STDMETHOD(Open)(const std::shared_ptr<ByteStream>& pChainedStream);

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

    static HRESULT ReplayJournalStream(
        const logger& pLog,
        const std::shared_ptr<ByteStream>& pFromStream,
        const std::shared_ptr<ByteStream>& pToStream);
    static HRESULT IsStreamJournaled(const logger& pLog, const std::shared_ptr<ByteStream>& pStream);

    virtual ~JournalingStream();
};

}  // namespace Orc

#pragma managed(pop)
