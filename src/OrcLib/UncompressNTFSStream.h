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

#include "boost/logic/tribool.hpp"

#pragma managed(push, off)

namespace Orc {

class NTFSStream;

class ORCLIB_API UncompressNTFSStream : public ChainingStream
{

public:
    UncompressNTFSStream(logger pLog);
    virtual ~UncompressNTFSStream(void);

    STDMETHOD(IsOpen)()
    {
        if (m_pChainedStream == NULL)
            return S_FALSE;
        return m_pChainedStream->IsOpen();
    };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_FALSE; };
    STDMETHOD(CanSeek)() { return S_OK; };

    //
    // ByteStream implementation
    //
    STDMETHOD(Open)(const std::shared_ptr<ByteStream>& pChainedStream, DWORD dwCompressionUnit);

    STDMETHOD(Open)(const std::shared_ptr<NTFSStream>& pChainedStream, DWORD dwCompressionUnit);

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

private:
    DWORD m_dwCompressionUnit;
    DWORD m_dwMaxCompressionUnit;
    ULONGLONG m_ullPosition;

    std::vector<boost::logic::tribool> m_IsBlockCompressed;

    HRESULT ReadCompressionUnit(DWORD dwNbCU, CBinaryBuffer& uncompressedData, __out_opt PULONGLONG pcbBytesRead);
};
}  // namespace Orc

#pragma managed(pop)
