//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "MemoryStream.h"

#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API MultiMemoryStream : public ByteStream
{

protected:
    BOOL m_bReadOnly;

public:
    MultiMemoryStream(logger pLog);
    ~MultiMemoryStream();

    //
    // CByteStream implementation
    //

    STDMETHOD(IsOpen)() { return S_FALSE; };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };

    STDMETHOD(OpenForReadWrite)();

    STDMETHOD(OpenForReadOnly)(__in PVOID pBuffer, __in DWORD cbBuffer);

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
    STDMETHOD(SetSize)(ULONG64 ullNewSize);

    STDMETHOD(Close)();
};
}  // namespace Orc

#pragma managed(pop)
