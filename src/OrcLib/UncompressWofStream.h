//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2021 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ChainingStream.h"

#include <boost/logic/tribool.hpp>

#include "NTFSStream.h"
#include "Stream/BufferStreamConcept.h"
#include "Stream/ByteStreamConcept.h"
#include "Filesystem/Ntfs/Compression/WofAlgorithm.h"
#include "Filesystem/Ntfs/Compression/WofStreamConcept.h"
#include "Filesystem/Ntfs/Compression/Engine/Nt/NtDecompressorConcept.h"

#pragma managed(push, off)

namespace Orc {

class NTFSStream;

class UncompressWofStream : public ChainingStream
{
public:
    using ByteStreamT = ByteStreamConcept<std::shared_ptr<NTFSStream>>;

    UncompressWofStream();
    virtual ~UncompressWofStream();

    STDMETHOD(IsOpen)()
    {
        if (m_pChainedStream == NULL)
            return S_FALSE;
        return m_pChainedStream->IsOpen();
    };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_FALSE; };
    STDMETHOD(CanSeek)() { return S_OK; };

    //
    // ByteStream implementation
    //
    STDMETHOD(Open)(const std::shared_ptr<ByteStream>& pChainedStream);

    STDMETHOD(Open)
    (const std::shared_ptr<NTFSStream>& ntfsStream, Ntfs::WofAlgorithm algorithm, uint64_t uncompressedSize);

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64 ullSize);

    STDMETHOD(Close)();

private:
    Ntfs::WofStreamConcept<ByteStreamT, NtDecompressorConcept> m_wofStream;
    BufferStreamConcept<std::vector<uint8_t>> m_buffer;
    uint64_t m_uncompressedSize;
};

}  // namespace Orc

#pragma managed(pop)
