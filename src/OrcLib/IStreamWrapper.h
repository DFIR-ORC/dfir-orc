//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <Objidl.h>
#include <memory>

#pragma managed(push, off)

namespace Orc {

class ByteStream;

class IStreamWrapper : public ::IStream
{
private:
    long m_refCount;
    std::shared_ptr<ByteStream> m_baseStream;

public:
    IStreamWrapper(const std::shared_ptr<ByteStream>& baseStream);
    virtual ~IStreamWrapper();

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

public:
    STDMETHOD(Read)(__out_bcount_part(cb, *pcbRead) void* pv, __in ULONG cb, __out_opt ULONG* pcbRead);

    STDMETHOD(Write)(__in_bcount(cb) const void* pv, __in ULONG cb, __out_opt ULONG* pcbWritten);

public:
    STDMETHOD(Seek)(__in LARGE_INTEGER dlibMove, __in DWORD dwOrigin, __out_opt ULARGE_INTEGER* plibNewPosition);

    STDMETHOD(SetSize)(__in ULARGE_INTEGER libNewSize);

    STDMETHOD(CopyTo)
    (IStream* pstm, ULARGE_INTEGER cb, __out_opt ULARGE_INTEGER* pcbRead, __out_opt ULARGE_INTEGER* pcbWritten);

    STDMETHOD(Commit)(DWORD grfCommitFlags);

    STDMETHOD(Revert)(void);

    STDMETHOD(LockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);

    STDMETHOD(UnlockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);

    STDMETHOD(Stat)(__RPC__out STATSTG* pstatstg, DWORD grfStatFlag);

    STDMETHOD(Clone)(__RPC__deref_out_opt IStream** ppstm);
};

}  // namespace Orc

#pragma managed(pop)
