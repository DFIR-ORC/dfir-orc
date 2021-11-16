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

#include "VolumeReader.h"
#include "MFTUtils.h"

#pragma managed(push, off)

namespace Orc {

class VolumeReader;
class MFTRecord;
class MftRecordAttribute;

class ORCLIB_API NTFSStream : public ByteStream
{

public:
    NTFSStream();
    ~NTFSStream();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {
        if (m_DataSize > 0LL)
            return (!m_DataSegments.empty()) && (m_pVolReader != nullptr) ? S_OK : S_FALSE;
        else
            return (m_pVolReader != nullptr) ? S_OK : S_FALSE;
    };
    STDMETHOD(CanRead)() { return S_OK; }
    STDMETHOD(CanWrite)() { return S_FALSE; }
    STDMETHOD(CanSeek)() { return S_OK; }

    STDMETHOD(OpenStream)
    (__in_opt const std::shared_ptr<VolumeReader>& pVolReader,
     __in_opt const std::shared_ptr<MftRecordAttribute>& pDataAttr);

    STDMETHOD(OpenAllocatedDataStream)
    (__in_opt const std::shared_ptr<VolumeReader>& pVolReader,
     __in_opt const std::shared_ptr<MftRecordAttribute>& pDataAttr);

    const std::vector<MFTUtils::DataSegment> DataSegments() const { return m_DataSegments; }

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytesToRead, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytesToRead,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)() { return m_DataSize; }
    STDMETHOD(SetSize)(ULONG64) { return E_NOTIMPL; }
    STDMETHOD(Close)();

private:
    std::shared_ptr<VolumeReader> m_pVolReader;
    std::vector<MFTUtils::DataSegment> m_DataSegments;

    ULONGLONG m_DataSize;  // Size of all data in stream
    ULONGLONG m_CurrentPosition;  // Current position in stream;

    ULONGLONG m_CurrentSegmentOffset;  // Current position inside the current segment
    std::vector<MFTUtils::DataSegment>::size_type m_CurrentSegmentIndex;  // Current segment index

    bool m_bAllocatedData;
};

}  // namespace Orc

#pragma managed(pop)
