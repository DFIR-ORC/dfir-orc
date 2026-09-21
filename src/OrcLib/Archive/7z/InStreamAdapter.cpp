//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Archive/7z/InStreamAdapter.h"

#include "ByteStream.h"

using namespace Orc::Archive;

STDMETHODIMP InStreamAdapter::Read(void* data, UInt32 size, UInt32* processedSize)
{
    ULONGLONG read = 0;
    HRESULT hr = m_stream->Read(data, size, &read);
    if (processedSize != NULL)
    {
        if (read > MAXDWORD)
        {
            // Unreachable in practice (the caller asks for at most a UInt32) and it used to return the ATL
            // registrar code E_ATL_VALUE_TOO_LARGE, which is 0x0215: a value SUCCEEDED() accepts, leaving
            // processedSize unwritten on what is meant to be a failure.
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }

        *processedSize = (UInt32)read;
    }

    // Transform S_FALSE to S_OK
    if (SUCCEEDED(hr))
    {
        return S_OK;
    }

    // Shadow copy volume Read can silently fail with some blocks and it will produce an error with ntfs compressed
    // files. That error should be ignored so the archive is not aborted.
    if (m_readErrorIsNotFailure)
    {
        Log::Error("Failed to read a stream to compress (archived item size: {}) [{}]", read, SystemError(hr));
        return S_OK;
    }

    return hr;
}

STDMETHODIMP InStreamAdapter::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
{
    LARGE_INTEGER move;
    ULARGE_INTEGER newPos;

    move.QuadPart = offset;

    HRESULT hr = m_stream->SetFilePointer(move.QuadPart, seekOrigin, &newPos.QuadPart);
    if (newPosition != NULL)
    {
        *newPosition = newPos.QuadPart;
    }

    return hr;
}

STDMETHODIMP InStreamAdapter::GetSize(UInt64* size)
{
    if (size == nullptr)
    {
        return E_POINTER;
    }

    *size = m_stream->GetSize();

    return S_OK;
}
