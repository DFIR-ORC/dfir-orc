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

class ORCLIB_API ByteStream
{
    friend class ChainingStream;

protected:
    virtual std::shared_ptr<ByteStream> _GetHashStream();

public:
    virtual ~ByteStream();

    virtual void Accept(ByteStreamVisitor& visitor) { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)() PURE;
    STDMETHOD(CanRead)() PURE;
    STDMETHOD(CanWrite)() PURE;
    STDMETHOD(CanSeek)() PURE;

    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead) PURE;

    STDMETHOD(Write)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten) PURE;

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

    STDMETHOD(Close)() PURE;

    static std::shared_ptr<ByteStream> GetStream(const OutputSpec& output);

    static std::shared_ptr<ByteStream> GetHashStream(const std::shared_ptr<ByteStream>& aStream);

    static HRESULT Get_IInStream(const std::shared_ptr<ByteStream>& aStream, ::IInStream** pInStream);
    static HRESULT Get_IStream(const std::shared_ptr<ByteStream>& aStream, ::IStream** pStream);
    static HRESULT
    Get_ISequentialStream(const std::shared_ptr<ByteStream>& aStream, ::ISequentialStream** pSequentialStream);
};
}  // namespace Orc

constexpr auto DEFAULT_READ_SIZE = (0x0200000);

#pragma managed(pop)
