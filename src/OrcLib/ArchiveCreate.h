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
#include "MFTRecord.h"
#include "Archive.h"

#include <memory>

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API ArchiveCreate : public Archive
{

protected:
    ULONG64 m_cbTotalData;  // # of bytes that needs to be compressed
    ULONG64 m_cbAddedSoFar;  // # of bytes that have been compressed

    DWORD m_XORPattern;

    ArchiveFormat m_Format;
    ArchiveItems m_Queue;

    std::shared_ptr<ByteStream> GetStreamToAdd(
        const std::shared_ptr<ByteStream>& astream,
        const std::wstring& strCabbedName,
        std::wstring& strPrefixedName);

    ArchiveCreate(logger pLog, bool bComputeHash, DWORD XORPattern);

public:
    static std::shared_ptr<ArchiveCreate> MakeCreate(ArchiveFormat fmt, logger pLog, bool bComputeHash = false);

    STDMETHOD(InitArchive)(__in PCWSTR pwzArchivePath, Archive::ArchiveCallback pCallback = nullptr) PURE;
    STDMETHOD(InitArchive)
    (__in const std::shared_ptr<ByteStream>& pOutputStream, Archive::ArchiveCallback pCallback = nullptr) PURE;

    STDMETHOD(SetCompressionLevel)(__in const std::wstring& strLevel) PURE;

    STDMETHOD(AddFile)(__in PCWSTR pwzNameInArchive, __in PCWSTR pwzFileName, bool bDeleteWhenDone);
    STDMETHOD(AddBuffer)(__in_opt PCWSTR pwzNameInArchive, __in PVOID pData, __in DWORD cbData);
    STDMETHOD(AddStream)
    (__in_opt PCWSTR pwzNameInArchive, __in_opt PCWSTR pwzPath, __in_opt const std::shared_ptr<ByteStream>& pStream);

    STDMETHOD(FlushQueue)() PURE;

    STDMETHOD(Complete)() PURE;
    STDMETHOD(Abort)() PURE;

    size_t GetArchiveSize();
    size_t GetArchivedSize();

    virtual ~ArchiveCreate(void);
};

}  // namespace Orc

#pragma managed(pop)
