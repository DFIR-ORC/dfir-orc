//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "FatFileInfo.h"
#include "FileSystem.h"
#include "FatStream.h"

#include "VolumeReader.h"
#include "NtDllExtension.h"

#include <sstream>

using namespace Orc;

FatFileInfo::FatFileInfo(
    LPCWSTR szComputerName,
    Intentions DefaultIntentions,
    const std::vector<Filter>& Filters,
    LPCWSTR szFullName,
    DWORD dwLen,
    const std::shared_ptr<VolumeReader>& pVolReader,
    const std::shared_ptr<FatFileEntry>& fileEntry,
    Authenticode& codeVerifyTrust)
    : FileInfo(szComputerName, pVolReader, DefaultIntentions, Filters, szFullName, dwLen, codeVerifyTrust)
    , m_FatFileEntry(fileEntry)
{
    m_hFile = INVALID_HANDLE_VALUE;
}

FatFileInfo::~FatFileInfo() {}

HRESULT FatFileInfo::Open()
{
    return E_FAIL;
}

std::shared_ptr<ByteStream> FatFileInfo::GetFileStream()
{
    if (m_FileStream == nullptr)
    {
        m_FileStream = std::make_shared<FatStream>(m_pVolReader, m_FatFileEntry);
    }

    return m_FileStream;
}

const std::unique_ptr<DataDetails>& FatFileInfo::GetDetails()
{
    if (m_Details == nullptr)
        m_Details.reset(new DataDetails());

    return m_Details;
}

bool FatFileInfo::IsDirectory()
{
    return m_FatFileEntry->IsFolder();
}

HRESULT FatFileInfo::WriteFileName(ITableOutput& output)
{
    return output.WriteString(m_FatFileEntry->GetLongName());
}

HRESULT FatFileInfo::WriteShortName(ITableOutput& output)
{
    return output.WriteString(m_FatFileEntry->m_Name);
}

HRESULT FatFileInfo::WriteAttributes(ITableOutput& output)
{
    DWORD ulAttrs = m_FatFileEntry->m_Attributes;

    if (m_FatFileEntry->IsFolder())
        ulAttrs |= FILE_ATTRIBUTE_DIRECTORY;
    else
        ulAttrs |= FILE_ATTRIBUTE_NORMAL;

    return output.WriteAttributes(ulAttrs);
}

HRESULT FatFileInfo::WriteSizeInBytes(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = output.WriteFileSize(0L, m_FatFileEntry->GetSize())))
    {
        return hr;
    }

    return S_OK;
}

HRESULT FatFileInfo::WriteCreationDate(ITableOutput& output)
{
    return output.WriteFileTime(m_FatFileEntry->m_CreationTime);
}

HRESULT FatFileInfo::WriteLastModificationDate(ITableOutput& output)
{
    return output.WriteFileTime(m_FatFileEntry->m_ModificationTime);
}

HRESULT FatFileInfo::WriteLastAccessDate(ITableOutput& output)
{
    return output.WriteFileTime(m_FatFileEntry->m_AccessTime);
}

HRESULT FatFileInfo::WriteRecordInUse(ITableOutput& output)
{
    return output.WriteBool(!m_FatFileEntry->IsDeleted());
}
