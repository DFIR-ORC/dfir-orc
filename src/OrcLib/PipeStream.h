//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"
#include "CriticalSection.h"

constexpr auto DEFAULT_PIPE_SIZE = (0x10000);

#pragma managed(push, off)

namespace Orc {

class PipeStream : public ByteStream
{

public:
    PipeStream(logger pLog)
        : ByteStream(std::move(pLog)) {};

    STDMETHOD(IsOpen)()
    {
        if (m_hReadPipe != INVALID_HANDLE_VALUE && m_hWritePipe != INVALID_HANDLE_VALUE)
            return S_OK;
        else
            return S_FALSE;
    };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };

    STDMETHOD(CreatePipe)(__in DWORD nSize = DEFAULT_PIPE_SIZE);

    STDMETHOD(Read)
    (__out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytesToRead,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);

    bool DataIsAvailable();

    HRESULT WINAPI Peek(
        _Out_opt_ LPVOID lpBuffer,
        _In_ DWORD nBufferSize,
        _Out_opt_ LPDWORD lpBytesRead,
        _Out_opt_ LPDWORD lpTotalBytesAvail);

    STDMETHOD(Close)();

    ~PipeStream();

private:
    CriticalSection m_cs;
    HANDLE m_hReadPipe = INVALID_HANDLE_VALUE;
    HANDLE m_hWritePipe = INVALID_HANDLE_VALUE;
};

}  // namespace Orc

#pragma managed(pop)
