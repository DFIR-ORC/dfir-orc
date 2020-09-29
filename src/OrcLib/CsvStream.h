//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"

#include "CsvCruncher.h"

#pragma managed(push, off)

namespace Orc::TableOutput::CSV {

class Stream : public ByteStream
{
private:
    Cruncher m_Cruncher;

    bool m_bReady = false;

public:
    Stream()
        : ByteStream()
        , m_Cruncher() {};

    STDMETHOD(Initialize)
    (bool bfirstRowIsColumnNames, WCHAR wcSeparator, WCHAR wcQuote, const WCHAR* szDateFormat, DWORD dwSkipLines);

    STDMETHOD(IsOpen)() { return S_OK; };
    STDMETHOD(CanRead)() { return S_FALSE; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_FALSE; };

    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesRead)
    {
        DBG_UNREFERENCED_PARAMETER(pBuffer);
        DBG_UNREFERENCED_PARAMETER(cbBytes);
        DBG_UNREFERENCED_PARAMETER(pcbBytesRead);
        return E_NOTIMPL;
    };

    STDMETHOD(Write)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
    {
        DBG_UNREFERENCED_PARAMETER(DistanceToMove);
        DBG_UNREFERENCED_PARAMETER(dwMoveMethod);
        DBG_UNREFERENCED_PARAMETER(pCurrPointer);
        return E_NOTIMPL;
    };

    STDMETHOD_(ULONG64, GetSize)() { return 0LL; }

    STDMETHOD(SetSize)(ULONG64) { return E_NOTIMPL; }

    STDMETHOD(Close)() { return S_OK; }

    virtual ~Stream();
};

}  // namespace Orc::TableOutput::CSV
#pragma managed(pop)
