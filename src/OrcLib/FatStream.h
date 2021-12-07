//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "ByteStream.h"

#include "SegmentDetails.h"

#pragma managed(push, off)

namespace Orc {

class VolumeReader;
class FatFileEntry;

class FatStream : public ByteStream
{
public:
    FatStream(const std::shared_ptr<VolumeReader>& pVolReader, const std::shared_ptr<FatFileEntry>& fileEntry);
    virtual ~FatStream();

    STDMETHOD(IsOpen)();
    STDMETHOD(CanRead)() { return S_OK; }
    STDMETHOD(CanWrite)() { return S_FALSE; }
    STDMETHOD(CanSeek)() { return S_OK; }

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytesToRead,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64) { return E_NOTIMPL; }
    STDMETHOD(Close)();

private:
    const std::shared_ptr<VolumeReader> m_pVolReader;
    const std::shared_ptr<FatFileEntry> m_FatFileEntry;
    ULONGLONG m_CurrentPosition;
    SegmentDetailsMap::const_iterator m_CurrentSegmentDetails;
};
}  // namespace Orc

#pragma managed(pop)
