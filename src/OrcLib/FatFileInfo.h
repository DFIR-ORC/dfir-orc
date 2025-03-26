//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "OrcLib.h"
#include "FileInfo.h"

#include <string>

#pragma managed(push, off)

namespace Orc {

class VolumeReader;
class DataDetails;
class FatFileEntry;

class FatFileInfo : public FileInfo
{
public:
    FatFileInfo(
        LPCWSTR szComputerName,
        Intentions DefaultIntentions,
        const std::vector<Filter>& Filters,
        LPCWSTR szFullName,
        DWORD dwLen,
        const std::shared_ptr<VolumeReader>& pVolReader,
        const std::shared_ptr<FatFileEntry>& fileEntry,
        Authenticode& codeVerifyTrust);
    virtual ~FatFileInfo();

    virtual HRESULT Open();

    virtual bool IsDirectory();
    virtual std::shared_ptr<ByteStream> GetFileStream();
    virtual const std::unique_ptr<DataDetails>& GetDetails();
    virtual bool ExceedsFileThreshold(DWORD nFileSizeHigh, DWORD nFileSizeLow) { return false; }

    static const ColumnNameDef g_FatColumnNames[];
    static const ColumnNameDef g_FatAliasNames[];

    const std::shared_ptr<FatFileEntry>& GetFatFileEntry() const { return m_FatFileEntry; }
    const std::shared_ptr<VolumeReader>& GetVolumeReader() const { return m_pVolReader; }

private:
    virtual HRESULT WriteFileName(ITableOutput& output);
    virtual HRESULT WriteShortName(ITableOutput& output);

    virtual HRESULT WriteAttributes(ITableOutput& output);
    virtual HRESULT WriteSizeInBytes(ITableOutput& output);

    virtual HRESULT WriteCreationDate(ITableOutput& output);
    virtual HRESULT WriteLastModificationDate(ITableOutput& output);
    virtual HRESULT WriteLastAccessDate(ITableOutput& output);

    virtual HRESULT WriteRecordInUse(ITableOutput& output);

private:
    const std::shared_ptr<FatFileEntry>& m_FatFileEntry;
    std::shared_ptr<ByteStream> m_FileStream;
    std::unique_ptr<DataDetails> m_Details;
};
}  // namespace Orc
#pragma managed(pop)
