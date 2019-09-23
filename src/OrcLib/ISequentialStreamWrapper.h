//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <memory>

#include <Objidl.h>

#pragma managed(push, off)

namespace Orc {

class ByteStream;

class ISequentialStreamWrapper : public ::ISequentialStream
{
private:
    long m_refCount;
    std::shared_ptr<ByteStream> m_baseStream;

public:
    ISequentialStreamWrapper(const std::shared_ptr<ByteStream>& baseStream);
    virtual ~ISequentialStreamWrapper();

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

public:
    STDMETHOD(Read)(__out_bcount_part(cb, *pcbRead) void* pv, __in ULONG cb, __out_opt ULONG* pcbRead);

    STDMETHOD(Write)(__in_bcount(cb) const void* pv, __in ULONG cb, __out_opt ULONG* pcbWritten);
};

}  // namespace Orc

#pragma managed(pop)
