//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <7zip/7zip.h>
#include "ZipCreate.h"

#include <filesystem>

#include <boost/algorithm/string.hpp>

#include "7zip/Archive/IArchive.h"
#include "7zip/ICoder.h"
#include "7zip/IPassword.h"

#include "ZipLibrary.h"
#include "PropVariant.h"
#include "FileStream.h"
#include "TemporaryStream.h"
#include "CryptoHashStream.h"
#include "InByteStreamWrapper.h"
#include "OutByteStreamWrapper.h"

#include "ArchiveUpdateCallback.h"
#include "ArchiveOpenCallback.h"

#include "CaseInsensitive.h"
#include "Temporary.h"

using namespace std;
using namespace lib7z;

namespace fs = std::filesystem;

using namespace Orc;

namespace {

void StoreFileHashes(OrcArchive::ArchiveItems& items, bool releaseInputStreams)
{
    for (auto& item : items)
    {
        if (item.currentStatus != OrcArchive::ArchiveItem::Status::Done)
        {
            Log::Warn("Unexpected archive status: {}", static_cast<size_t>(item.currentStatus));
            continue;
        }

        auto hashstream = std::dynamic_pointer_cast<CryptoHashStream>(item.Stream);
        if (hashstream)
        {
            // An item will be convertible to 'CryptoHashStream' only when processed the first time.
            // Items that are compressed before 'bFinal == true' must be ignored as their hash were
            // already processed.
            hashstream->GetMD5(item.MD5);
            hashstream->GetSHA1(item.SHA1);

            // TODO: see if we want also to calculate sha256
            // hashstream->GetSHA256(item.SHA256);
        }

        if (releaseInputStreams)
        {
            item.Stream = nullptr;
        }
    }
}

}  // namespace

ZipCreate::ZipCreate(bool bComputeHash)
    : ArchiveCreate(bComputeHash)
    , m_FormatGUID(CLSID_NULL)
    , m_CompressionLevel(Fast)
{
}

ZipCreate::CompressionLevel ZipCreate::GetCompressionLevel(const std::wstring& strLevel)
{
    if (strLevel.empty())
    {
        return ZipCreate::CompressionLevel::Fast;
    }

    if (equalCaseInsensitive(strLevel, L"None"))
        return ZipCreate::CompressionLevel::None;
    if (equalCaseInsensitive(strLevel, L"Fastest"))
        return ZipCreate::CompressionLevel::Fastest;
    if (equalCaseInsensitive(strLevel, L"Fast"))
        return ZipCreate::CompressionLevel::Fast;
    if (equalCaseInsensitive(strLevel, L"Normal"))
        return ZipCreate::CompressionLevel::Normal;
    if (equalCaseInsensitive(strLevel, L"Maximum"))
        return ZipCreate::CompressionLevel::Maximum;
    if (equalCaseInsensitive(strLevel, L"Ultra"))
        return ZipCreate::CompressionLevel::Ultra;

    Log::Warn(L"Selecting default compression level (unrecognised parameter was '{}')", strLevel);

    return ZipCreate::CompressionLevel::Fast;
}

HRESULT ZipCreate::SetCompressionLevel(const CComPtr<IOutArchive>& pArchiver, CompressionLevel level)
{
    HRESULT hr = E_FAIL;

    Log::Debug(L"ZipCreate: {}: set compression level to {}", m_ArchiveName, static_cast<size_t>(level));

    if (!pArchiver)
    {
        Log::Error("ZipCreate: Failed to update compression level: invalid pointer");
        return E_POINTER;
    }

    const size_t numProps = 1;
    const wchar_t* names[numProps] = {L"x"};
    CPropVariant values[numProps] = {static_cast<UInt32>(level)};  // test with

    CComPtr<ISetProperties> setter;
    if (FAILED(hr = pArchiver->QueryInterface(IID_ISetProperties, reinterpret_cast<void**>(&setter))))
    {
        Log::Error("Failed to retrieve IID_ISetProperties [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = setter->SetProperties(names, values, numProps)))
    {
        Log::Error("Failed to set properties [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

STDMETHODIMP ZipCreate::InitArchive(const fs::path& path, OrcArchive::ArchiveCallback pCallback)
{
    return InitArchive(path.c_str(), pCallback);
}

STDMETHODIMP ZipCreate::InitArchive(PCWSTR pwzArchivePath, OrcArchive::ArchiveCallback pCallback)
{
    HRESULT hr = E_FAIL;

    auto filestream = make_shared<FileStream>();

    m_ArchiveName = pwzArchivePath;
    if (FAILED(
            hr = filestream->OpenFile(pwzArchivePath, GENERIC_WRITE | GENERIC_READ, 0L, NULL, CREATE_ALWAYS, 0L, NULL)))
    {
        Log::Error(L"Failed to open '{}' for writing [{}]", pwzArchivePath, SystemError(hr));
        return hr;
    }

    return InitArchive(filestream, pCallback);
}

STDMETHODIMP
ZipCreate::InitArchive(__in const std::shared_ptr<ByteStream>& pOutputStream, OrcArchive::ArchiveCallback pCallback)
{
    m_FormatGUID = CLSID_NULL;

    switch (m_Format)
    {
        case ArchiveFormat::SevenZip:
            m_FormatGUID = CLSID_CFormat7z;
            break;
        case ArchiveFormat::Zip:
            m_FormatGUID = CLSID_CFormatZip;
            break;
        default:
            Log::Debug("No format specified for a archive, defaulting to 7zip");
            m_FormatGUID = CLSID_CFormat7z;
            break;
    }

    if (m_FormatGUID == CLSID_NULL)
    {
        Log::Debug("Failed to find a format matching extension, defaulting to 7zip");
        m_FormatGUID = CLSID_CFormat7z;
    }

    m_ArchiveStream = pOutputStream;
    m_Callback = pCallback;
    return S_OK;
}

HRESULT ZipCreate::SetCompressionLevel(__in const std::wstring& level)
{
    if (level.empty())
    {
        Log::Debug("Specified compression level is empty");
        return S_OK;
    }

    Log::Debug(L"Updated internal compression level to {}", level);
    m_CompressionLevel = GetCompressionLevel(level);

    return S_OK;
}

STDMETHODIMP ZipCreate::Internal_FlushQueue(bool bFinal)
{
    HRESULT hr = E_FAIL;

    const auto pZipLib = ZipLibrary::GetZipLibrary();
    if (pZipLib == nullptr)
    {
        Log::Error(L"FAILED to load 7zip.dll");
        return E_FAIL;
    }

    std::shared_ptr<ByteStream> outStream;

    {
        CComPtr<IOutArchive> pArchiver;
        if (FAILED(hr = pZipLib->CreateObject(&m_FormatGUID, &IID_IOutArchive, reinterpret_cast<void**>(&pArchiver))))
        {
            Log::Error(L"Failed to create archiver");
            return hr;
        }

        if (FAILED(hr = SetCompressionLevel(pArchiver, m_CompressionLevel)))
        {
            Log::Error(
                L"Failed to set compression level to {} [{}]",
                static_cast<size_t>(m_CompressionLevel),
                SystemError(hr));
            return hr;
        }

        if (pArchiver == nullptr)
            return E_NOT_VALID_STATE;

        if (m_Queue.empty())
            return S_OK;

        if (!m_Items.empty())
        {
            if (!m_TempStream || (hr = m_TempStream->CanRead()) != S_OK)
            {
                Log::Error(L"Temp archive stream cannot be read");
                return E_UNEXPECTED;
            }

            if (FAILED(hr = m_TempStream->SetFilePointer(0LL, FILE_BEGIN, NULL)))
            {
                Log::Error(L"Failed to rewind temp stream [{}]", SystemError(hr));
                return hr;
            }

            CComPtr<InByteStreamWrapper> infile = new InByteStreamWrapper(m_TempStream);

            CComQIPtr<IInArchive, &IID_IInArchive> inarchive(pArchiver);
            if (inarchive != nullptr)
            {
                CComPtr<ArchiveOpenCallback> callback = new ArchiveOpenCallback();
                if (FAILED(hr = inarchive->Open(infile, nullptr, callback)))
                {
                    Log::Error(L"Failed to open archive stream [{}]", SystemError(hr));
                    return hr;
                }
            }
        }

        CComPtr<OutByteStreamWrapper> outFile;
        if (bFinal)
        {
            outFile = new OutByteStreamWrapper(m_ArchiveStream, false);
        }
        else
        {
            // Creating a new temporary stream
            std::shared_ptr<TemporaryStream> pNewTemp = std::make_shared<TemporaryStream>();

            fs::path tempdir;
            if (!m_ArchiveName.empty())
            {
                tempdir = fs::path(m_ArchiveName).parent_path();
            }
            auto tempstr = tempdir.wstring();
            if (FAILED(hr = pNewTemp->Open(tempstr, L"ZipStream", 100 * 1024 * 1024)))
            {
                Log::Error(L"Failed to create temp stream [{}]", SystemError(hr));
                return hr;
            }

            outStream = pNewTemp;
            outFile = new OutByteStreamWrapper(pNewTemp, false);
        }

        {
            concurrency::critical_section::scoped_lock sl(m_cs);

            size_t first_added = m_Items.size();
            m_Items.reserve(m_Items.size() + m_Queue.size());
            std::move(begin(m_Queue), end(m_Queue), std::back_inserter(m_Items));
            m_Queue.clear();

            for (size_t i = first_added; i < m_Items.size(); ++i)
            {
                if (m_Items[i].Index != (UINT)-1)
                    m_Indexes[m_Items[i].Index] = i;
            }
        }

        CComPtr<ArchiveUpdateCallback> callback =
            new ArchiveUpdateCallback(m_Items, m_Indexes, bFinal, m_Callback, m_Password);

        if ((hr = pArchiver->UpdateItems(outFile, (UInt32)m_Items.size(), callback)) != S_OK)
        {
            // returning S_FALSE also indicates error
            Log::Error(L"Failed to update '{}' [{}]", m_ArchiveName, SystemError(hr));
            return hr;
        }
    }

    // Fix index for empty files which is not updated as "SetOperationResult" is not called by 7z as for non-empty
    // files.
    for (size_t i = 0; i < m_Items.size(); ++i)
    {
        auto& item = m_Items[i];
        if (item.currentStatus == OrcArchive::ArchiveItem::Selected && item.Size == 0)
        {
            assert(m_Indexes.size() <= m_Items.size());
            item.currentStatus = OrcArchive::ArchiveItem::Done;
            item.Index = m_Indexes.size();
            m_Indexes[item.Index] = i;
        }
    }

    const bool kReleaseInputStreams = true;
    StoreFileHashes(m_Items, kReleaseInputStreams);

    auto leftover_found = std::find_if(begin(m_Items), end(m_Items), [](const ArchiveItem& item) {
        return item.Index == (DWORD)-1;
    });  // Look for unarchived items.

    if (leftover_found != end(m_Items))
    {
        ArchiveItems NewItems;
        NewItems.resize(m_Items.size());
        m_Queue.resize(m_Items.size());

        auto ends = std::partition_copy(
            begin(m_Items), end(m_Items), begin(NewItems), begin(m_Queue), [](const ArchiveItem& item) {
                return item.Index != (DWORD)-1;
            });

        NewItems.erase(ends.first, end(NewItems));
        m_Queue.erase(ends.second, end(m_Queue));

        std::swap(m_Items, NewItems);
    }

    if (bFinal)
    {
        for (const auto& item : m_Queue)
        {
            Log::Warn(
                L"Queued item '{}' was not included in archive ({} bytes)",
                item.NameInArchive,
                item.Stream != nullptr ? item.Stream->GetSize() : 0LL);
        }
        m_TempStream = nullptr;
    }
    else
    {
        m_TempStream = dynamic_pointer_cast<TemporaryStream>(outStream);
    }
    return S_OK;
}

STDMETHODIMP ZipCreate::FlushQueue()
{
    return Internal_FlushQueue(false);
}

STDMETHODIMP ZipCreate::Complete()
{
    HRESULT hr = E_FAIL;

    if (FAILED(Internal_FlushQueue(true)))
        return hr;

    std::for_each(begin(m_Items), end(m_Items), [](ArchiveItem& item) {
        // Now that the archive is complete, get back to items and close associated streams
        if (item.Stream != nullptr)
        {
            item.Stream->Close();
            item.Stream = nullptr;
        }
    });

    if (m_TempStream != nullptr)
    {
        if (m_ArchiveName.empty())
        {
            if (FAILED(hr = m_TempStream->MoveTo(m_ArchiveStream)))
            {
                Log::Error(L"Failed to copy Temp stream to output stream [{}]", SystemError(hr));
                return hr;
            }
            m_ArchiveStream->Close();
        }
        else
        {
            m_ArchiveStream->Close();
            if (FAILED(hr = m_TempStream->MoveTo(m_ArchiveName.c_str())))
            {
                Log::Error(L"Failed to copy Temp stream to output stream [{}]", SystemError(hr));
                return hr;
            }
        }
    }
    else
    {
        m_ArchiveStream->Close();
    }
    return S_OK;
}

STDMETHODIMP ZipCreate::Abort()
{
    return S_OK;
}

ZipCreate::~ZipCreate(void) {}
