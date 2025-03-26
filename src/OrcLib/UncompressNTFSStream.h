//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2023 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "NTFSStream.h"

#include "boost/logic/tribool.hpp"

#pragma managed(push, off)

namespace Orc {

namespace Stream {
class VolumeStreamReader;
}

class UncompressNTFSStream : public NTFSStream
{

public:
    UncompressNTFSStream();

    STDMETHOD(OpenAllocatedDataStream)
    (__in_opt const std::shared_ptr<VolumeReader>& pVolReader,
     __in_opt const std::shared_ptr<MftRecordAttribute>& pDataAttr);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    ~UncompressNTFSStream();

private:
    HRESULT ReadRaw(CBinaryBuffer& buffer, size_t length);

private:
    std::unique_ptr<Stream::VolumeStreamReader> m_volume;
    DWORD m_dwCompressionUnit;
    DWORD m_dwMaxCompressionUnit;
    ULONGLONG m_ullPosition;

    std::vector<boost::logic::tribool> m_IsBlockCompressed;

    HRESULT ReadCompressionUnit(DWORD dwNbCU, CBinaryBuffer& uncompressedData, __out_opt PULONGLONG pcbBytesRead);
};
}  // namespace Orc

#pragma managed(pop)
