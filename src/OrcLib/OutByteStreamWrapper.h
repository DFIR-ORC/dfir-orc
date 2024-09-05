//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "7zip/CPP/7zip/IStream.h"

#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class ByteStream;

class OutByteStreamWrapper : public IOutStream
{
private:
    long m_refCount;
    std::shared_ptr<ByteStream> m_baseStream;
    bool m_bCloseOnDelete;

public:
    OutByteStreamWrapper(const std::shared_ptr<ByteStream>& baseStream, bool bCloseOnDelete = true);
    virtual ~OutByteStreamWrapper();

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // ISequentialOutStream
    STDMETHOD(Write)(const void* data, UInt32 size, UInt32* processedSize);

    // IOutStream
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition);
    STDMETHOD(SetSize)(UInt64 newSize);
};

}  // namespace Orc
#pragma managed(pop)
