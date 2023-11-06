//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <7zip/7zip.h>

#include "ArchiveOpenCallback.h"

using namespace Orc;

ArchiveOpenCallback::ArchiveOpenCallback()
    : m_refCount(0)
{
}

ArchiveOpenCallback::~ArchiveOpenCallback() {}

STDMETHODIMP ArchiveOpenCallback::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = reinterpret_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IArchiveOpenCallback)
    {
        *ppvObject = static_cast<IArchiveOpenCallback*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_ICryptoGetTextPassword)
    {
        *ppvObject = static_cast<ICryptoGetTextPassword*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ArchiveOpenCallback::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

STDMETHODIMP_(ULONG) ArchiveOpenCallback::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

STDMETHODIMP ArchiveOpenCallback::SetTotal(const UInt64*, const UInt64*)
{
    return S_OK;
}

STDMETHODIMP ArchiveOpenCallback::SetCompleted(const UInt64*, const UInt64*)
{
    return S_OK;
}

STDMETHODIMP ArchiveOpenCallback::CryptoGetTextPassword(BSTR*)
{
    // TODO: support passwords
    return E_NOTIMPL;
}
