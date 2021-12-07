//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>
#include <filesystem>

#include "ByteStream.h"
#include "VolumeReader.h"
#include "MFTRecord.h"
#include "Archive.h"

#pragma managed(push, off)

namespace Orc {

class ArchiveCreate : public OrcArchive
{

protected:
    ULONG64 m_cbTotalData;  // # of bytes that needs to be compressed
    ULONG64 m_cbAddedSoFar;  // # of bytes that have been compressed

    ArchiveFormat m_Format;
    ArchiveItems m_Queue;

    std::shared_ptr<ByteStream> GetStreamToAdd(const std::shared_ptr<ByteStream>& astream);

    ArchiveCreate(bool bComputeHash);

public:
    static std::shared_ptr<ArchiveCreate> MakeCreate(ArchiveFormat fmt, bool bComputeHash = false);

    STDMETHOD(InitArchive)(__in PCWSTR pwzArchivePath, OrcArchive::ArchiveCallback pCallback = nullptr) PURE;
    STDMETHOD(InitArchive)
    (__in const std::filesystem::path& path, OrcArchive::ArchiveCallback pCallback = nullptr) PURE;
    STDMETHOD(InitArchive)
    (__in const std::shared_ptr<ByteStream>& pOutputStream, OrcArchive::ArchiveCallback pCallback = nullptr) PURE;

    STDMETHOD(SetCompressionLevel)(__in const std::wstring& strLevel) PURE;

    STDMETHOD(AddFile)(__in PCWSTR pwzNameInArchive, __in PCWSTR pwzFileName, bool bDeleteWhenDone);
    STDMETHOD(AddBuffer)(__in_opt PCWSTR pwzNameInArchive, __in PVOID pData, __in DWORD cbData);
    STDMETHOD(AddStream)
    (__in_opt PCWSTR pwzNameInArchive, __in_opt PCWSTR pwzPath, __in_opt const std::shared_ptr<ByteStream>& pStream);
    STDMETHODIMP AddStream(
        __in_opt PCWSTR pwzNameInArchive,
        __in_opt PCWSTR pwzPath,
        __in_opt const std::shared_ptr<ByteStream>& pStream,
        ArchiveItem::ArchivedCallback itemArchivedCallback);

    STDMETHOD(FlushQueue)() PURE;

    STDMETHOD(Complete)() PURE;
    STDMETHOD(Abort)() PURE;

    size_t GetArchiveSize();
    size_t GetArchivedSize();

    virtual ~ArchiveCreate(void);
};

}  // namespace Orc

#pragma managed(pop)
