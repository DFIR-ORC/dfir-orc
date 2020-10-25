//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Archive/7z/OutStreamAdapter.h"

#include "ByteStream.h"

using namespace Orc::Archive;

STDMETHODIMP OutStreamAdapter::Write(const void* data, UInt32 size, UInt32* processedSize)
{
    ULARGE_INTEGER written = {0};
    HRESULT hr = m_stream->Write((const PVOID)data, size, &written.QuadPart);
    if (processedSize != NULL)
    {
        *processedSize = written.u.LowPart;
    }

    return hr;
}

STDMETHODIMP OutStreamAdapter::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
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

STDMETHODIMP OutStreamAdapter::SetSize(UInt64 newSize)
{
    ULARGE_INTEGER size;
    size.QuadPart = newSize;
    return m_stream->SetSize(size.QuadPart);
}
