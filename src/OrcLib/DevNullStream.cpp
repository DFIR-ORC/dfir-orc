//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "DevNullStream.h"

using namespace Orc;

DevNullStream::DevNullStream()
    : ByteStream()
{
}

DevNullStream::~DevNullStream(void) {}

HRESULT DevNullStream::Close()
{
    return S_OK;
}

HRESULT DevNullStream::Open()
{
    return S_OK;
}

HRESULT DevNullStream::Read_(
    __out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytesToRead,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pBuffer);

    if (cbBytesToRead > MAXDWORD)
        return E_INVALIDARG;
    *pcbBytesRead = 0;
    return S_OK;
}

HRESULT
DevNullStream::Write_(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out PULONGLONG pcbBytesWritten)
{

    DBG_UNREFERENCED_PARAMETER(pBuffer);
    if (cbBytes > MAXDWORD)
        return E_INVALIDARG;
    *pcbBytesWritten = cbBytes;
    return S_OK;
}

HRESULT
DevNullStream::SetFilePointer(__in LONGLONG lDistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pqwCurrPointer)
{
    LARGE_INTEGER liDistanceToMove = {0};
    LARGE_INTEGER liNewFilePointer = {0};

    DBG_UNREFERENCED_PARAMETER(dwMoveMethod);

    DWORD retval = 0;
    liDistanceToMove.QuadPart = lDistanceToMove;

    if (pqwCurrPointer)
        *pqwCurrPointer = liNewFilePointer.QuadPart;

    return retval;
}

ULONG64 DevNullStream::GetSize()
{
    LARGE_INTEGER Size = {0};
    return Size.QuadPart;
}

HRESULT DevNullStream::SetSize(ULONG64 ullNewSize)
{
    DBG_UNREFERENCED_PARAMETER(ullNewSize);
    return S_OK;
}
