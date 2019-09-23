//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "7zip/IStream.h"

#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class ByteStream;

class InByteStreamWrapper
    : public ::IInStream
    , public IStreamGetSize
{
private:
    long m_refCount;
    std::shared_ptr<ByteStream> m_baseStream;

public:
    InByteStreamWrapper(const std::shared_ptr<ByteStream>& baseStream);
    virtual ~InByteStreamWrapper();

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // ISequentialInStream
    STDMETHOD(Read)(void* data, UInt32 size, UInt32* processedSize);

    // IInStream
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition);

    // IStreamGetSize
    STDMETHOD(GetSize)(UInt64* size);
};

}  // namespace Orc

#pragma managed(pop)
