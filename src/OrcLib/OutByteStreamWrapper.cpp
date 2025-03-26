//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "OutByteStreamWrapper.h"

using namespace Orc;

OutByteStreamWrapper::OutByteStreamWrapper(const std::shared_ptr<ByteStream>& baseStream, bool bCloseOnDelete)
    : m_refCount(0)
    , m_baseStream(baseStream)
    , m_bCloseOnDelete(bCloseOnDelete)
{
}

OutByteStreamWrapper::~OutByteStreamWrapper()
{
    if (m_baseStream->IsOpen() == S_OK && m_bCloseOnDelete)
        m_baseStream->Close();
    m_baseStream = nullptr;
}

HRESULT STDMETHODCALLTYPE OutByteStreamWrapper::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_ISequentialOutStream)
    {
        *ppvObject = static_cast<ISequentialOutStream*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IOutStream)
    {
        *ppvObject = static_cast<IOutStream*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE OutByteStreamWrapper::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE OutByteStreamWrapper::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

STDMETHODIMP OutByteStreamWrapper::Write(const void* data, UInt32 size, UInt32* processedSize)
{
    ULARGE_INTEGER written = {0, 0};
    HRESULT hr = m_baseStream->Write((const PVOID)data, size, &written.QuadPart);
    if (processedSize != NULL)
    {
        *processedSize = written.u.LowPart;
    }
    return hr;
}

STDMETHODIMP OutByteStreamWrapper::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
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

STDMETHODIMP OutByteStreamWrapper::SetSize(UInt64 newSize)
{
    ULARGE_INTEGER size;
    size.QuadPart = newSize;
    return m_baseStream->SetSize(size.QuadPart);
}
