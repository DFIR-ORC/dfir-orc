//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <memory>

#include "ArchiveCreate.h"

#include "FileStream.h"
#include "MemoryStream.h"
#include "NTFSStream.h"
#include "CryptoHashStream.h"

#include "ZipCreate.h"

using namespace std;

using namespace Orc;

std::shared_ptr<ArchiveCreate> ArchiveCreate::MakeCreate(ArchiveFormat fmt, logger pLog, bool bComputeHash)
{
    std::shared_ptr<ArchiveCreate> retval;
    switch (fmt)
    {
        case ArchiveFormat::Zip:
        case ArchiveFormat::SevenZip:
        case ArchiveFormat::SevenZipSupported:
            retval = std::shared_ptr<ZipCreate>(new ZipCreate(std::move(pLog), bComputeHash));
            break;
        default:
            return nullptr;
    }
    retval->m_Format = fmt;
    return std::move(retval);
}

ArchiveCreate::ArchiveCreate(logger pLog, bool bComputeHash)
    : Archive(std::move(pLog), bComputeHash)
{
}

ArchiveCreate::~ArchiveCreate(void) {}

std::shared_ptr<ByteStream> ArchiveCreate::GetStreamToAdd(const std::shared_ptr<ByteStream>& astream)
{
    HRESULT hr = E_FAIL;

    if (astream == nullptr)
        return nullptr;

    // Move back to the begining of the stream
    astream->SetFilePointer(0LL, FILE_BEGIN, nullptr);

    if (!m_bComputeHash)
    {
        return astream;
    }

    auto pHashStream = make_shared<CryptoHashStream>(_L_);
    hr = pHashStream->OpenToRead(CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1, astream);
    if (FAILED(hr))
    {
        return nullptr;
    }

    return pHashStream;
}

STDMETHODIMP ArchiveCreate::AddFile(__in PCWSTR pwzNameInArchive, __in PCWSTR pwzFileName, bool bDeleteWhenDone)
{
    HRESULT hr = E_FAIL;

    ArchiveItem item;

    item.bDeleteWhenAdded = bDeleteWhenDone;

    {
        auto stream = make_shared<FileStream>(_L_);

        if (FAILED(
                hr = stream->OpenFile(
                    pwzFileName,
                    GENERIC_READ | (bDeleteWhenDone ? DELETE : 0L),
                    FILE_SHARE_READ | (bDeleteWhenDone ? FILE_SHARE_DELETE : 0L),
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN | (bDeleteWhenDone ? FILE_FLAG_DELETE_ON_CLOSE : 0L),
                    NULL)))
        {
            log::Error(_L_, hr, L"Failed to open file to archive %s\r\n", pwzFileName);
            return hr;
        }

        item.Path = pwzFileName;
        item.NameInArchive = pwzNameInArchive;
        item.Stream = GetStreamToAdd(stream);
    }

    if (item.Stream)
    {
        m_Queue.push_back(std::move(item));
    }
    else
        return E_FAIL;
    return S_OK;
}

STDMETHODIMP ArchiveCreate::AddBuffer(__in_opt PCWSTR pwzNameInArchive, __in PVOID pData, __in DWORD cbData)
{
    HRESULT hr = E_FAIL;

    ArchiveItem item;

    item.NameInArchive = pwzNameInArchive;

    auto stream = make_shared<MemoryStream>(_L_);

    if (FAILED(hr = stream->OpenForReadOnly(pData, cbData)))
    {
        log::Error(_L_, hr, L"Failed to open buffer to archive\r\n");
        return hr;
    }

    item.Stream = GetStreamToAdd(stream);

    if (item.Stream)
    {
        m_Queue.push_back(std::move(item));
    }
    else
        return E_FAIL;

    return S_OK;
}

STDMETHODIMP ArchiveCreate::AddStream(
    __in_opt PCWSTR pwzNameInArchive,
    __in_opt PCWSTR pwzPath,
    __in_opt const std::shared_ptr<ByteStream>& pStream)
{
    return AddStream(pwzNameInArchive, pwzPath, pStream, {});
}

STDMETHODIMP ArchiveCreate::AddStream(
    __in_opt PCWSTR pwzNameInArchive,
    __in_opt PCWSTR pwzPath,
    __in_opt const std::shared_ptr<ByteStream>& pStream,
    ArchiveItem::ArchivedCallback itemArchivedCallback)
{
    ArchiveItem item;

    item.NameInArchive = pwzNameInArchive;
    item.Stream = GetStreamToAdd(pStream);
    item.Size = pStream->GetSize();
    item.Path = pwzPath;
    item.m_archivedCallback = itemArchivedCallback;

    if (item.Stream == nullptr)
    {
        itemArchivedCallback(E_FAIL);
        return E_FAIL;
    }

    m_Queue.push_back(std::move(item));
    return S_OK;
}
