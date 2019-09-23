//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ISequentialStreamWrapper.h"

#include "ByteStream.h"

using namespace Orc;

ISequentialStreamWrapper::ISequentialStreamWrapper(const std::shared_ptr<ByteStream>& baseStream)
    : m_refCount(0)
    , m_baseStream(baseStream)
{
}

ISequentialStreamWrapper::~ISequentialStreamWrapper() {}

HRESULT STDMETHODCALLTYPE ISequentialStreamWrapper::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = reinterpret_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IStream)
    {
        *ppvObject = static_cast<ISequentialStream*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ISequentialStreamWrapper::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE ISequentialStreamWrapper::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

HRESULT STDMETHODCALLTYPE
ISequentialStreamWrapper::Read(__out_bcount_part(cb, *pcbRead) void* pv, __in ULONG cb, __out_opt ULONG* pcbRead)
{
    if (m_baseStream == nullptr)
        return E_POINTER;

    if (pcbRead != nullptr)
        *pcbRead = 0L;

    ULONGLONG ullRead = 0LL;
    HRESULT hr = m_baseStream->Read(pv, cb, &ullRead);

    if (FAILED(hr))
        return hr;

    if (pcbRead != nullptr)
        *pcbRead = static_cast<ULONG>(ullRead);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
ISequentialStreamWrapper::Write(__in_bcount(cb) const void* pv, __in ULONG cb, __out_opt ULONG* pcbWritten)
{
    if (m_baseStream == nullptr)
        return E_POINTER;

    if (pcbWritten != nullptr)
        *pcbWritten = 0L;

    ULONGLONG ullWritten = 0LL;
    HRESULT hr = m_baseStream->Write((const PVOID)pv, cb, &ullWritten);

    if (FAILED(hr))
        return hr;

    if (pcbWritten != nullptr)
        *pcbWritten = static_cast<ULONG>(ullWritten);
    return S_OK;
}
