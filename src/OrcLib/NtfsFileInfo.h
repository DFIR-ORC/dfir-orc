//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "FileInfo.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API NtfsFileInfo : public FileInfo
{
public:
    NtfsFileInfo(
        std::wstring strComputerName,
        const std::shared_ptr<VolumeReader>& pVolReader,
        Intentions dwDefaultIntentions,
        const std::vector<Filter>& filters,
        LPCWSTR szFullFileName,
        DWORD dwLen,
        Authenticode& verifytrust);
    virtual ~NtfsFileInfo();

    virtual HRESULT OpenHash();
    virtual HRESULT HandleIntentions(const Intentions& intention, ITableOutput& output);

    // abstract methods
    virtual bool IsDirectory() = 0;
    virtual std::shared_ptr<ByteStream> GetFileStream() = 0;
    virtual const std::unique_ptr<DataDetails>& GetDetails() = 0;
    virtual bool ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow) = 0;
    virtual ULONGLONG GetFileReferenceNumber() = 0;

    // ntfs specific write functions
    virtual HRESULT WriteADS(ITableOutput& output) = 0;
    virtual HRESULT WriteFilenameFlags(ITableOutput& output) = 0;
    virtual HRESULT WriteFilenameID(ITableOutput& output) = 0;
    virtual HRESULT WriteDataID(ITableOutput& output) = 0;
    virtual HRESULT WriteUSN(ITableOutput& output) = 0;
    virtual HRESULT WriteFRN(ITableOutput& output) = 0;
    virtual HRESULT WriteParentFRN(ITableOutput& output) = 0;
    virtual HRESULT WriteSecDescrID(ITableOutput& output) = 0;

    virtual HRESULT WriteLastAttrChangeDate(ITableOutput& output);
    virtual HRESULT WriteFileNameLastModificationDate(ITableOutput& output);
    virtual HRESULT WriteFileNameLastAttrModificationDate(ITableOutput& output);
    virtual HRESULT WriteFileNameCreationDate(ITableOutput& output);
    virtual HRESULT WriteFileNameLastAccessDate(ITableOutput& output);
    virtual HRESULT WriteExtendedAttributes(ITableOutput& output);
    virtual HRESULT WriteEASize(ITableOutput& output);
    virtual HRESULT WriteFilenameIndex(ITableOutput& output);
    virtual HRESULT WriteDataIndex(ITableOutput& output);
    virtual HRESULT WriteSnapshotID(ITableOutput& output);

    static const ColumnNameDef g_NtfsColumnNames[];
    static const ColumnNameDef g_NtfsAliasNames[];
};

}  // namespace Orc

#pragma managed(pop)
