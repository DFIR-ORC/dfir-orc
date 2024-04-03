//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ByteStream.h"
#include "Buffer.h"

#include <safeint.h>

// The purpose of this class is to complement the MemoryStream for small amount of data
// Orc::Buffer is more efficient in this case than the Virtual alloc'd memory of MemoryStream
// Typically used in GetThis for resident data

namespace Orc {

template <size_t _DeclElts>
class BufferStream : public ByteStream
{

public:
    BufferStream()
        : ByteStream()
    {
    }
    virtual ~BufferStream() {}

    STDMETHOD(IsOpen)() { return m_IsOpen ? S_OK : S_FALSE; };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_OK; };
    STDMETHOD(CanSeek)() { return S_OK; };

    STDMETHOD(Open)()
    {
        m_IsOpen = true;
        return S_OK;
    }

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)() { return m_Buffer.size(); }
    STDMETHOD(SetSize)(ULONG64 newSize)
    {
        using namespace msl::utilities;
        try
        {
            m_Buffer.resize(SafeInt<ULONG>(newSize));
        }
        catch (const Exception& e)
        {
            Log::Error(L"Failed to allocate {} bytes in BufferStream: {}", newSize, e.Description);
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    STDMETHOD(Close)() { return S_OK; }

private:
    Buffer<BYTE, _DeclElts> m_Buffer;
    size_t m_dwCurrFilePointer = 0;
    bool m_IsOpen = false;
};

template <size_t _DeclElts>
inline STDMETHODIMP BufferStream<_DeclElts>::Read_(PVOID pBuffer, ULONGLONG cbBytes, PULONGLONG pcbBytesRead)
{
    using namespace msl::utilities;

    size_t dwBytesToRead =
        std::min(static_cast<size_t>(m_Buffer.size() - m_dwCurrFilePointer), static_cast<size_t>(cbBytes));

    if (dwBytesToRead == 0)
    {
        if (pcbBytesRead)
            *pcbBytesRead = 0LLU;
        return S_OK;
    }

    CopyMemory(pBuffer, m_Buffer.get(SafeInt<ULONG>(m_dwCurrFilePointer)), dwBytesToRead);
    m_dwCurrFilePointer += dwBytesToRead;

    if (pcbBytesRead)
    {
        *pcbBytesRead = dwBytesToRead;
    }
    return S_OK;
}

template <size_t _DeclElts>
inline STDMETHODIMP 
    BufferStream<_DeclElts>::Write_(const PVOID pBuffer, ULONGLONG cbBytesToWrite, PULONGLONG pcbBytesWritten)
{
    using namespace msl::utilities;
    HRESULT hr = E_FAIL;

    if (pcbBytesWritten)
        *pcbBytesWritten = 0LLU;

    if (cbBytesToWrite == 0)
        return S_OK;

    if (m_dwCurrFilePointer + cbBytesToWrite < m_dwCurrFilePointer)
    {
        Log::Error("File pointer overflowed");
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }

    //
    // Check whether we need to expand the buffer
    //
    if (m_Buffer.size() < cbBytesToWrite + m_dwCurrFilePointer)
    {
        //
        // Expand the buffer
        //
        try
        {
            m_Buffer.resize(SafeInt<ULONG>(cbBytesToWrite + m_dwCurrFilePointer));
        }
        catch (const Exception& e)
        {
            Log::Error(
                L"Failed SetFilePointer({}) to resize BufferStream: {}",
                cbBytesToWrite + m_dwCurrFilePointer,
                e.Description);
            return E_OUTOFMEMORY;
        }
    }

    //
    // Copy data into the stream buffer
    //
    CopyMemory(m_Buffer.get(SafeInt<size_t>(m_dwCurrFilePointer)), pBuffer, SafeInt<size_t>(cbBytesToWrite));

    m_dwCurrFilePointer += static_cast<size_t>(cbBytesToWrite);

    if (pcbBytesWritten)
    {
        *pcbBytesWritten = cbBytesToWrite;
    }
    return S_OK;
}

template <size_t _DeclElts>
inline STDMETHODIMP
    BufferStream<_DeclElts>::SetFilePointer(LONGLONG DistanceToMove, DWORD dwMoveMethod, PULONG64 pCurrPointer)
{
    HRESULT hr = E_FAIL;
    DWORD dwNewFilePointer = 0;

    if (DistanceToMove > MAXDWORD)
        return E_INVALIDARG;
    LARGE_INTEGER liDistanceToMove;
    liDistanceToMove.QuadPart = DistanceToMove;

    switch (dwMoveMethod)
    {
        case SEEK_CUR:
            dwNewFilePointer = LONG(m_dwCurrFilePointer) + liDistanceToMove.LowPart;
            break;
        case SEEK_END:
            dwNewFilePointer = (LONG)m_Buffer.size() + liDistanceToMove.LowPart;
            break;

        case SEEK_SET:
            dwNewFilePointer = liDistanceToMove.LowPart;
            break;

        default:
            return E_INVALIDARG;
    }

    //
    // Clip the pointer to [0, m_Buffer.size()]
    //
    if (dwNewFilePointer > m_Buffer.size())
    {
        try
        {
            m_Buffer.resize(dwNewFilePointer);
        }
        catch (const Exception& e)
        {
            Log::Error(L"Failed SetFilePointer({}) to resize BufferStream: {}", dwNewFilePointer, e.Description);
            return E_OUTOFMEMORY;
        }
    }
    m_dwCurrFilePointer = dwNewFilePointer;

    if (pCurrPointer)
    {
        *pCurrPointer = m_dwCurrFilePointer;
    }
    return S_OK;
}

}  // namespace Orc
