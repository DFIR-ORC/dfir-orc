//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Archive.h"

#include <vector>
#include <string>
#include <functional>

#pragma managed(push, off)

namespace Orc {

class ArchiveExtract : public OrcArchive
{
public:
    typedef std::function<std::shared_ptr<ByteStream>(OrcArchive::ArchiveItem& item)> MakeOutputStream;
    typedef std::function<HRESULT(std::shared_ptr<ByteStream>& stream)> MakeArchiveStream;

    static std::shared_ptr<ArchiveExtract> MakeExtractor(ArchiveFormat fmt, bool bComputeHash = false);

    typedef std::function<bool(const std::wstring& strNameInArchive)> ItemShouldBeExtractedCallback;

protected:
    MakeOutputStream m_MakeWriteAbleStream;
    MakeArchiveStream m_MakeArchiveStream;
    ItemShouldBeExtractedCallback m_ShouldBeExtracted;

    ArchiveExtract(bool bComputeHash)
        : OrcArchive(bComputeHash) {};

    std::vector<std::pair<std::wstring, std::wstring>> m_ExtractedItems;

public:
    STDMETHOD(Extract)(__in PCWSTR pwzArchivePath, __in PCWSTR pwzExtractRootDir, __in PCWSTR szSDDL);

    STDMETHOD(Extract)
    (__in PCWSTR pwzArchivePath,
     __in PCWSTR pwzExtractRootDir,
     __in PCWSTR szSDDL,
     __in const std::vector<std::wstring>& ListToExtract);

    STDMETHOD(Extract)
    (__in const std::shared_ptr<ByteStream>& pArchiveStream, __in PCWSTR pwzExtractRootDir, __in PCWSTR szSDDL);

    STDMETHOD(Extract)
    (__in const std::shared_ptr<ByteStream>& pArchiveStream,
     __in PCWSTR pwzExtractRootDir,
     __in PCWSTR szSDDL,
     __in const std::vector<std::wstring>& ListToExtract);

    STDMETHOD(Extract)
    (__in MakeArchiveStream makeArchiveStream,
     __in const ItemShouldBeExtractedCallback pShouldBeExtracted,
     __in MakeOutputStream MakeWriteAbleStream) PURE;

    const std::vector<std::pair<std::wstring, std::wstring>>& GetExtractedItems() { return m_ExtractedItems; };

    ~ArchiveExtract(void);
};
}  // namespace Orc

#pragma managed(pop)
