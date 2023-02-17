//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"
#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {

class FileMappingStream : public ByteStream
{
private:
    HANDLE m_hMapping;
    BYTE* m_pMapped;

    ULONGLONG m_ullCurrentPosition;
    ULONGLONG m_ullDataSize;
    ULONGLONG m_ullCommittedSize;

    DWORD m_dwProtect;
    DWORD m_dwViewProtect;
    DWORD m_dwPageProtect;

    HRESULT CommitSize(ULONGLONG ullNewSize);

public:
    FileMappingStream()
        : ByteStream()
        , m_hMapping(INVALID_HANDLE_VALUE)
        , m_pMapped(nullptr)
        , m_dwProtect(0L)
        , m_dwPageProtect(0L)
        , m_dwViewProtect(0L)
        , m_ullCommittedSize(0LL)
        , m_ullCurrentPosition(0LL)
        , m_ullDataSize(0LL) {};

    STDMETHOD(IsOpen)() { return m_hMapping != INVALID_HANDLE_VALUE ? S_OK : S_FALSE; };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return m_dwProtect == PAGE_READWRITE ? S_OK : S_FALSE; };
    STDMETHOD(CanSeek)() { return S_OK; };

    STDMETHOD(Open)(_In_ HANDLE hFile, _In_ DWORD flProtect, _In_ ULONGLONG ullMaximumSize, _In_opt_ LPCWSTR lpName);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64);

    STDMETHOD(Close)();

    CBinaryBuffer GetMappedData()
    {
        CBinaryBuffer retval;
        retval.SetData(m_pMapped, (size_t)m_ullDataSize);
        return retval;
    }

    virtual ~FileMappingStream(void);
};

}  // namespace Orc

#pragma managed(pop)
