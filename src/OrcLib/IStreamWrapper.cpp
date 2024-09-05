//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "IStreamWrapper.h"

#include "ByteStream.h"

#include "VolumeReader.h"

using namespace Orc;

IStreamWrapper::IStreamWrapper(const std::shared_ptr<ByteStream>& baseStream)
    : m_refCount(0)
    , m_baseStream(baseStream)
{
}

IStreamWrapper::~IStreamWrapper() {}

HRESULT STDMETHODCALLTYPE IStreamWrapper::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = reinterpret_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IStream)
    {
        *ppvObject = static_cast<IStream*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE IStreamWrapper::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE IStreamWrapper::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

HRESULT STDMETHODCALLTYPE
IStreamWrapper::Read(__out_bcount_part(cb, *pcbRead) void* pv, __in ULONG cb, __out_opt ULONG* pcbRead)
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
IStreamWrapper::Write(__in_bcount(cb) const void* pv, __in ULONG cb, __out_opt ULONG* pcbWritten)
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

STDMETHODIMP
IStreamWrapper::Seek(__in LARGE_INTEGER dlibMove, __in DWORD dwOrigin, __out_opt ULARGE_INTEGER* plibNewPosition)
{
    if (m_baseStream == nullptr)
        return E_POINTER;

    DWORD dwMoveMethod;

    switch (dwOrigin)
    {
        case STREAM_SEEK_SET:
            dwMoveMethod = FILE_BEGIN;
            break;
        case STREAM_SEEK_CUR:
            dwMoveMethod = FILE_CURRENT;
            break;
        case STREAM_SEEK_END:
            dwMoveMethod = FILE_END;
            break;
        default:
            return STG_E_INVALIDFUNCTION;
            break;
    }

    return m_baseStream->SetFilePointer(
        dlibMove.QuadPart, dwMoveMethod, plibNewPosition == nullptr ? nullptr : &(plibNewPosition->QuadPart));
}

STDMETHODIMP IStreamWrapper::SetSize(__in ULARGE_INTEGER libNewSize)
{
    if (m_baseStream == nullptr)
        return E_POINTER;

    return m_baseStream->SetSize(libNewSize.QuadPart);
}

STDMETHODIMP IStreamWrapper::CopyTo(
    IStream* pstm,
    ULARGE_INTEGER cb,
    __out_opt ULARGE_INTEGER* pcbRead,
    __out_opt ULARGE_INTEGER* pcbWritten)
{
    HRESULT hr = E_FAIL;
    if (m_baseStream == nullptr)
        return E_POINTER;

    ULARGE_INTEGER uliCopied;
    uliCopied.QuadPart = 0LL;

    CBinaryBuffer buffer(true);
    buffer.SetCount(DEFAULT_READ_SIZE);

    ULONGLONG ullRead = 0LL, ullWritten = 0LL;

    while (uliCopied.QuadPart < cb.QuadPart)
    {
        ULONGLONG ullToCopy = std::min<ULONGLONG>(DEFAULT_READ_SIZE, cb.QuadPart - uliCopied.QuadPart);
        ULONGLONG ullThisRead = 0LL;
        if (FAILED(hr = m_baseStream->Read(buffer.GetData(), ullToCopy, &ullThisRead)))
        {
            if (pcbRead != nullptr)
                pcbRead->QuadPart = ullRead + ullThisRead;
            if (pcbWritten != nullptr)
                pcbWritten->QuadPart = ullWritten;
            return hr;
        }

        if (ullThisRead == 0LL)
        {
            // No read indicates end of copy (EOF, whatever)
            break;
        }

        ullRead += ullThisRead;

        ULONG ullThisWrite = 0LL;
        if (FAILED(hr = pstm->Write(buffer.GetData(), static_cast<ULONG>(ullThisRead), &ullThisWrite)))
        {
            if (pcbRead != nullptr)
                pcbRead->QuadPart = ullRead;
            if (pcbWritten != nullptr)
                pcbWritten->QuadPart = ullWritten + ullThisWrite;
            return hr;
        }
        ullWritten += ullThisWrite;

        uliCopied.QuadPart += ullThisWrite;
    }

    if (pcbRead != nullptr)
        pcbRead->QuadPart = ullRead;
    if (pcbWritten != nullptr)
        pcbWritten->QuadPart = ullWritten;

    return S_OK;
}

STDMETHODIMP IStreamWrapper::Commit(DWORD grfCommitFlags)
{
    DBG_UNREFERENCED_PARAMETER(grfCommitFlags);
    return E_NOTIMPL;
}

STDMETHODIMP IStreamWrapper::Revert(void)
{
    return E_NOTIMPL;
}

STDMETHODIMP IStreamWrapper::LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
    DBG_UNREFERENCED_PARAMETER(libOffset);
    DBG_UNREFERENCED_PARAMETER(cb);
    DBG_UNREFERENCED_PARAMETER(dwLockType);
    return E_NOTIMPL;
}

STDMETHODIMP IStreamWrapper::UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
{
    DBG_UNREFERENCED_PARAMETER(libOffset);
    DBG_UNREFERENCED_PARAMETER(cb);
    DBG_UNREFERENCED_PARAMETER(dwLockType);
    return E_NOTIMPL;
}

STDMETHODIMP IStreamWrapper::Stat(__RPC__out STATSTG* pstatstg, DWORD grfStatFlag)
{
    if (!pstatstg)
        return E_POINTER;

    ZeroMemory(pstatstg, sizeof(STATSTG));

    pstatstg->grfLocksSupported = FALSE;
    pstatstg->type = STGTY_STREAM;
    pstatstg->cbSize.QuadPart = m_baseStream->GetSize();

    if (m_baseStream->CanRead() == S_OK && m_baseStream->CanWrite() == S_OK)
        pstatstg->grfMode = STGM_READWRITE;
    else if (m_baseStream->CanRead() == S_OK)
        pstatstg->grfMode = STGM_READ;
    else if (m_baseStream->CanWrite() == S_OK)
        pstatstg->grfMode = STGM_WRITE;

    return S_OK;
}

STDMETHODIMP IStreamWrapper::Clone(__RPC__deref_out_opt IStream** ppstm)
{
    DBG_UNREFERENCED_PARAMETER(ppstm);
    return E_NOTIMPL;
}
