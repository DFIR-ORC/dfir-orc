//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ByteStreamVisitor.h"

#include <memory>

#pragma managed(push, off)

struct IInStream;
struct IStream;
struct ISequentialStream;

namespace Orc {

class OutputSpec;
class CryptoHashStream;

class ByteStreamVisitor;

class ByteStream
{
    friend class ChainingStream;

protected:
    virtual std::shared_ptr<ByteStream> _GetHashStream();

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead) PURE;

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten) PURE;

public:
    ByteStream()
        : m_readCount(0)
        , m_totalRead(0)
        , m_writeCount(0)
        , m_totalWritten(0)
    {
    }

    virtual ~ByteStream();

    virtual void Accept(ByteStreamVisitor& visitor) { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)() PURE;
    STDMETHOD(CanRead)() PURE;
    STDMETHOD(CanWrite)() PURE;
    STDMETHOD(CanSeek)() PURE;

    HRESULT Read(
        __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
        __in ULONGLONG cbBytes,
        __out_opt PULONGLONG pcbBytesRead)
    {
        uint64_t read = 0;
        const auto hr = Read_(pBuffer, cbBytes, &read);
        if (pcbBytesRead)
        {
            *pcbBytesRead = read;
        }

        m_readCount++;
        m_totalRead += read;
        return hr;
    }

    uint64_t TotalRead() const { return m_totalRead; }
    uint64_t TotalWritten() const { return m_totalWritten; }

    HRESULT
    Write(__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten)
    {
        uint64_t write = 0;
        const auto hr = Write_(pBuffer, cbBytes, &write);
        if (pcbBytesWritten)
        {
            *pcbBytesWritten = write;
        }

        m_writeCount++;
        m_totalWritten += write;
        return hr;
    }

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer) PURE;

    STDMETHOD(CopyTo)(__in const std::shared_ptr<ByteStream>& pOutStream, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(CopyTo)(__in ByteStream& pOutStream, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(CopyTo)
    (__in const std::shared_ptr<ByteStream>& pOutStream,
     __in const ULONGLONG ullChunk,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(CopyTo)
    (__in ByteStream& pOutStream, __in const ULONGLONG ullChunk, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD_(ULONG64, GetSize)() PURE;
    STDMETHOD(SetSize)(ULONG64) PURE;

    STDMETHOD(Clone)(std::shared_ptr<ByteStream>& clone) { return E_NOTIMPL; }

    STDMETHOD(Close)() PURE;

    static std::shared_ptr<ByteStream> GetStream(const OutputSpec& output);
    static std::shared_ptr<ByteStream> GetHashStream(const std::shared_ptr<ByteStream>& aStream);

    static HRESULT Get_IInStream(const std::shared_ptr<ByteStream>& aStream, ::IInStream** pInStream);
    static HRESULT Get_IStream(const std::shared_ptr<ByteStream>& aStream, ::IStream** pStream);
    static HRESULT
    Get_ISequentialStream(const std::shared_ptr<ByteStream>& aStream, ::ISequentialStream** pSequentialStream);

    // This is a workaround over the size being use by some streams which could get huge while the stream is queue
    // for later use. A better way to handle this would be to easily Close then Open the stream back. Unfortunately the
    // design does not allow this currently.
    virtual HRESULT ShrinkContext() { return S_OK; }

private:
    uint64_t m_readCount;
    uint64_t m_totalRead;
    uint64_t m_writeCount;
    uint64_t m_totalWritten;
};

constexpr auto DEFAULT_READ_SIZE = (0x0200000);

}  // namespace Orc

#pragma managed(pop)
