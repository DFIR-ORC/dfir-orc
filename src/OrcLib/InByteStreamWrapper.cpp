//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "InByteStreamWrapper.h"

#include "ByteStream.h"

using namespace Orc;

InByteStreamWrapper::InByteStreamWrapper(const std::shared_ptr<ByteStream>& baseStream)
    : m_refCount(0)
    , m_baseStream(baseStream)
{
}

InByteStreamWrapper::~InByteStreamWrapper()
{
    m_baseStream = nullptr;
}

HRESULT STDMETHODCALLTYPE InByteStreamWrapper::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = reinterpret_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_ISequentialInStream)
    {
        *ppvObject = static_cast<ISequentialInStream*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IInStream)
    {
        *ppvObject = static_cast<IInStream*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IStreamGetSize)
    {
        *ppvObject = static_cast<IStreamGetSize*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE InByteStreamWrapper::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE InByteStreamWrapper::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

STDMETHODIMP InByteStreamWrapper::Read(void* data, UInt32 size, UInt32* processedSize)
{
    ULONGLONG read = 0;
    HRESULT hr = m_baseStream->Read(data, size, &read);
    if (processedSize != NULL)
    {
        if (read > MAXDWORD)
            return E_ATL_VALUE_TOO_LARGE;
        *processedSize = (UInt32)read;
    }
    // Transform S_FALSE to S_OK
    return SUCCEEDED(hr) ? S_OK : hr;
}

STDMETHODIMP InByteStreamWrapper::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
{
    LARGE_INTEGER move;
    ULARGE_INTEGER newPos;

    move.QuadPart = offset;

    HRESULT hr = m_baseStream->SetFilePointer(move.QuadPart, seekOrigin, &newPos.QuadPart);
    if (newPosition != NULL)
    {
        *newPosition = newPos.QuadPart;
    }
    return hr;
}

STDMETHODIMP InByteStreamWrapper::GetSize(UInt64* size)
{
    if (size == nullptr)
        return E_POINTER;

    *size = m_baseStream->GetSize();

    return S_OK;
}
