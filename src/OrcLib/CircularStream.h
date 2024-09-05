//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "TaskUtils.h"

#include "ByteStream.h"

#include "CircularStorage.h"

class TASKUTILS_API CircularStream : public ByteStream
{

protected:
    CircularStorage m_Store;
    CRITICAL_SECTION m_cs;

    ULONGLONG m_ullReadPointer;
    ULONGLONG m_ullReadCounter;
    ULONGLONG m_ullWriteCounter;

public:
    CircularStream(LogFileWriter* pW);
    ~CircularStream();

    STDMETHOD(IsOpen)() { return m_Store.IsInitialized(); };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };

    //
    // CByteStream implementation
    //
    STDMETHOD(OpenForReadWrite)(DWORD dwInitialSize, DWORD dwMaximum, DWORD dwTimeOut);

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
};
