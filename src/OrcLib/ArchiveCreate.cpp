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
#include "XORStream.h"

#include "ZipCreate.h"
#include "CabCreate.h"

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
        case ArchiveFormat::Cabinet:
            retval = std::shared_ptr<CabCreate>(new CabCreate(std::move(pLog), bComputeHash));
            break;
        default:
            return nullptr;
    }
    retval->m_Format = fmt;
    return std::move(retval);
}

ArchiveCreate::ArchiveCreate(logger pLog, bool bComputeHash, DWORD XORPattern)
    : Archive(std::move(pLog), bComputeHash)
    , m_XORPattern(XORPattern)
{
}

ArchiveCreate::~ArchiveCreate(void) {}

std::shared_ptr<ByteStream> ArchiveCreate::GetStreamToAdd(
    const std::shared_ptr<ByteStream>& astream,
    const std::wstring& strCabbedName,
    std::wstring& strPrefixedName)
{
    HRESULT hr = E_FAIL;

    if (astream == nullptr)
        return nullptr;

    // Move back to the begining of the stream
    astream->SetFilePointer(0LL, FILE_BEGIN, nullptr);

    if (m_XORPattern != 0L)
    {
        WCHAR szPrefixedName[MAX_PATH];

        shared_ptr<XORStream> pXORStream = make_shared<XORStream>(_L_);

        if (FAILED(hr = pXORStream->SetXORPattern(m_XORPattern)))
        {
            return nullptr;
        }

        if (FAILED(hr = pXORStream->XORPrefixFileName(strCabbedName.c_str(), szPrefixedName, MAX_PATH)))
        {
            return nullptr;
        }

        strPrefixedName.assign(szPrefixedName);

        if (m_bComputeHash)
        {
            shared_ptr<CryptoHashStream> pHashStream = make_shared<CryptoHashStream>(_L_);

            if (FAILED(
                    hr = pHashStream->OpenToRead(
                        static_cast<SupportedAlgorithm>(SupportedAlgorithm::MD5 | SupportedAlgorithm::SHA1), astream)))
                return nullptr;
            if (FAILED(hr = pXORStream->OpenForXOR(pHashStream)))
                return nullptr;

            return pXORStream;
        }
        else
        {
            if (FAILED(hr = pXORStream->OpenForXOR(astream)))
                return nullptr;
            return pXORStream;
        }
    }
    else
    {
        strPrefixedName = strCabbedName;

        if (m_bComputeHash)
        {
            shared_ptr<CryptoHashStream> pHashStream = make_shared<CryptoHashStream>(_L_);

            if (FAILED(
                    hr = pHashStream->OpenToRead(
                        static_cast<SupportedAlgorithm>(SupportedAlgorithm::MD5 | SupportedAlgorithm::SHA1), astream)))
                return nullptr;
            return pHashStream;
        }
        else
            return astream;
    }
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
        item.Stream = GetStreamToAdd(stream, item.NameInArchive, item.NameInArchive);
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

    item.Stream = GetStreamToAdd(stream, pwzNameInArchive, item.NameInArchive);

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
    ArchiveItem item;

    item.NameInArchive = pwzNameInArchive;
    item.Stream = GetStreamToAdd(pStream, item.NameInArchive, item.NameInArchive);
    item.Size = pStream->GetSize();
    item.Path = pwzPath;

    if (item.Stream)
    {
        m_Queue.push_back(std::move(item));
    }
    else
        return E_FAIL;
    return S_OK;
}
