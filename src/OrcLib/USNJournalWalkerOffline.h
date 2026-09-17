//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "IUSNJournalWalker.h"
#include "USNJournalWalkerBase.h"

#include "NtfsDataStructures.h"

#include "winioctl.h"

#include <utility>
#include <vector>

#pragma managed(push, off)

namespace Orc {

class ByteStream;
class MFTRecord;

class USNJournalWalkerOffline
    : public USNJournalWalkerBase
    , public IUSNJournalWalker
{
public:
    USNJournalWalkerOffline();
    virtual ~USNJournalWalkerOffline();

    const std::shared_ptr<ByteStream>& GetUsnJournal() const;
    void SetUsnJournal(const std::shared_ptr<ByteStream>& usnJournal);

    // from IUSNJournalWalker
    virtual HRESULT Initialize(const std::shared_ptr<Location>& loc);
    virtual HRESULT EnumJournal(const IUSNJournalWalker::Callbacks& pCallbacks);
    virtual HRESULT ReadJournal(const IUSNJournalWalker::Callbacks& pCallbacks);

    // static functions
    static void FillUSNRecord(USN_RECORD& record, MFTRecord* pElt, const PFILE_NAME pFileName);

    // Find the first consistent version 2 record at or after 'pChunk'. Damaged bytes do not stop the search: they are
    // passed over, and 'unrecognizedBytes' counts the non-zero ones (zero padding between records is normal). When the
    // chunk ends before a record can be checked or read whole, returns E_FAIL with 'shouldReadAnotherChunk' set and
    // 'adjustmentOffset' giving how many bytes, from the end of the chunk, must be read again.
    static HRESULT FindNextUSNRecord(
        BYTE* pChunk,
        BYTE* pMaxChunkPosition,
        BYTE** nextUSNRecord,
        bool& shouldReadAnotherChunk,
        ULONG64& adjustmentOffset,
        ULONG64& unrecognizedBytes);

    static DWORD GetBufferSize();
    static void SetBufferSize(DWORD size);

private:
    // [start, end) offsets in a chunk buffer.
    using ByteRange = std::pair<ULONG64, ULONG64>;

    // Read 'length' bytes of the journal at 'offset' into 'buffer'. When the read fails, the chunk is read again
    // sector by sector: 'badRanges' then lists the sectors that still failed, whose bytes in 'buffer' are not valid,
    // and the error that refused the chunk is returned. 'bytesRead' is how much of 'buffer' to consider.
    HRESULT
    ReadChunk(ULONG64 offset, BYTE* buffer, ULONG64 length, ULONG64& bytesRead, std::vector<ByteRange>& badRanges);

    // Report every record found in [begin, end). Returns how many bytes at the end could not be checked yet because
    // the range ends before the record or header they start.
    ULONG64
    ParseRecords(BYTE* begin, BYTE* end, const IUSNJournalWalker::Callbacks& pCallbacks, ULONG64& unrecognizedBytes);

    std::shared_ptr<Location> m_location;
    std::shared_ptr<ByteStream> m_USNJournal;

    static DWORD m_BufferSize;
};  // USNJournalWalkerOffline

}  // namespace Orc

#pragma managed(pop)
