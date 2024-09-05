//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <atlbase.h>

#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"

#include "ArchiveExtract.h"

#pragma managed(push, off)

namespace Orc {

class ArchiveExtractCallback
    : public IArchiveExtractCallback
    , public ICryptoGetTextPassword
{
public:
private:
    long m_refCount;

    CComPtr<IInArchive> m_archiveHandler;
    ArchiveExtract::MakeOutputStream m_MakeWriteAbleStream;

    bool m_bComputeHash;

    std::wstring m_Password;

    ArchiveExtract::ItemShouldBeExtractedCallback m_ShouldBeExtracted = nullptr;
    OrcArchive::ArchiveItems& m_Extracted;
    OrcArchive::ArchiveCallback m_pCallback;

    OrcArchive::ArchiveItem m_currentItem;

public:
    ArchiveExtractCallback(
        const CComPtr<IInArchive>& archiveHandler,
        const ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted,
        OrcArchive::ArchiveItems& Extracted,
        ArchiveExtract::MakeOutputStream MakeWriteAbleStream,
        OrcArchive::ArchiveCallback pCallback,
        bool bComputeHash,
        const std::wstring& pwd = L"");

    virtual ~ArchiveExtractCallback();

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IProgress
    STDMETHOD(SetTotal)(UInt64 size);
    STDMETHOD(SetCompleted)(const UInt64* completeValue);

    // IArchiveExtractCallback
    STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode);
    STDMETHOD(PrepareOperation)(Int32 askExtractMode);
    STDMETHOD(SetOperationResult)(Int32 resultEOperationResult);

    // ICryptoGetTextPassword
    STDMETHOD(CryptoGetTextPassword)(BSTR* password);

private:
    std::shared_ptr<ByteStream> GetStreamToWrite();

    STDMETHOD(GetPropertyFilePath)(UInt32 index);
    STDMETHOD(GetPropertyAttributes)(UInt32 index);
    STDMETHOD(GetPropertyIsDir)(UInt32 index);
    STDMETHOD(GetPropertyModifiedTime)(UInt32 index);
    STDMETHOD(GetPropertySize)(UInt32 index);
};

}  // namespace Orc

#pragma managed(pop)
