//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Archive/7z/Archive7z.h"

#include <Windows.h>

#include <7zip/extras.h>

#include "Archive/Item.h"
#include "Archive/7z/InStreamAdapter.h"
#include "Archive/7z/OutStreamAdapter.h"
#include "Archive/7z/ArchiveOpenCallback.h"
#include "Archive/7z/ArchiveUpdateCallback.h"
#include "ByteStream.h"

using namespace Orc::Archive;
using namespace Orc;

namespace {

#ifdef _7ZIP_STATIC
// Static linking requires calling specifics functions.
// Call '::Lib7z::Instance()' to initialize the singleton
class Lib7z
{
    Lib7z::Lib7z(const Lib7z&) = delete;
    Lib7z& Lib7z::operator=(const Lib7z&) = delete;

    Lib7z::Lib7z()
    {
        ::lib7zCrcTableInit();
        NArchive::N7z::Register();
        NArchive::NZip::Register();
        NCompress::RegisterCodecCopy();
        NCompress::NBcj::RegisterCodecBCJ();
        NCompress::NBcj2::RegisterCodecBCJ2();
        NCompress::NLzma::RegisterCodecLZMA();
        NCompress::NLzma2::RegisterCodecLZMA2();
        NCompress::NDeflate::RegisterCodecDeflate();
        NCompress::NDeflate::RegisterCodecDeflate64();

        NCrypto::N7z::RegisterCodec_7zAES();
        NCrypto::RegisterCodecAES256CBC();
    }

public:
    static Lib7z& Instance()
    {
        static Lib7z lib;
        return lib;
    }
};

#endif  // _7ZIP_STATIC

GUID ToGuid(Archive::Format format)
{
    switch (format)
    {
        case Archive::Format::k7z:
            return CLSID_CFormat7z;
        case Archive::Format::k7zZip:
            return CLSID_CFormatZip;
        default:
            Log::Error("Unexpected archive format, defaulting to 7z");
            return CLSID_CFormat7z;
    }
}

enum class Lib7zCompressionLevel
{
    None = 0,
    Fastest = 1,
    Fast = 3,
    Normal = 5,
    Maximum = 7,
    Ultra = 9
};

Lib7zCompressionLevel ToLib7zLevel(CompressionLevel level)
{
    switch (level)
    {
        case CompressionLevel::kNone:
            return Lib7zCompressionLevel::None;
        case CompressionLevel::kFastest:
            return Lib7zCompressionLevel::Fastest;
        case CompressionLevel::kFast:
            return Lib7zCompressionLevel::Fast;
        case CompressionLevel::kNormal:
            return Lib7zCompressionLevel::Normal;
        case CompressionLevel::kMaximum:
            return Lib7zCompressionLevel::Maximum;
        case CompressionLevel::kUltra:
            return Lib7zCompressionLevel::Ultra;
        case CompressionLevel::kDefault:
            return Lib7zCompressionLevel::Normal;
        default:
            return Lib7zCompressionLevel::Normal;
    }
}

void SetCompressionLevel(const CComPtr<IOutArchive>& archiver, CompressionLevel level, std::error_code& ec)
{
    Log::Debug("Archive7z: SetCompressionLevel to {}", static_cast<std::underlying_type_t<CompressionLevel>>(level));

    CMyComPtr<ISetProperties> setProperties;
    HRESULT hr = archiver->QueryInterface(IID_ISetProperties, (void**)&setProperties);
    if (setProperties == nullptr)
    {
        ec.assign(hr, std::system_category());
        Log::Error("Failed to set compresion level on IID_ISetProperties [{}]", ec);
        return;
    }

    const uint8_t numProps = 1;
    const wchar_t* names[numProps] = {L"x"};
    NWindows::NCOM::CPropVariant values[numProps] = {static_cast<UINT32>(::ToLib7zLevel(level))};

    hr = setProperties->SetProperties(names, values, numProps);
    if (FAILED(hr))
    {
        ec.assign(hr, std::system_category());
        Log::Error("Failed to change compresion level while setting property [{}]", ec);
        return;
    }
}

}  // namespace

Archive7z::Archive7z(Format format, Archive::CompressionLevel level, std::wstring password)
    : m_format(format)
    , m_compressionLevel(level)
    , m_password(std::move(password))
{
#ifdef _7ZIP_STATIC
    ::Lib7z::Instance();
#endif  // _7ZIP_STATIC
}

void Archive7z::Add(std::unique_ptr<Item> item)
{
    m_items.emplace_back(std::move(item));
}

void Archive7z::Add(Items items)
{
    std::move(std::begin(items), std::end(items), std::back_inserter(m_items));
}

void Archive7z::Compress(
    const std::shared_ptr<ByteStream>& outputArchive,
    const std::shared_ptr<ByteStream>& inputArchive,
    std::error_code& ec)
{
    if (m_items.empty() && inputArchive == nullptr)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    CComPtr<IOutArchive> archiver;
    const auto formatGuid = ::ToGuid(m_format);
    HRESULT hr = ::CreateObject(&formatGuid, &IID_IOutArchive, reinterpret_cast<void**>(&archiver));
    if (FAILED(hr))
    {
        ec.assign(hr, std::system_category());
        Log::Error(L"Failed to retrieve IID_IOutArchive [{}]", ec);
        return;
    }

    ::SetCompressionLevel(archiver, m_compressionLevel, ec);
    if (ec)
    {
        Log::Error(
            L"Failed to update compression level to {} [{}]",
            static_cast<std::underlying_type_t<Archive::CompressionLevel>>(m_compressionLevel),
            ec);
        return;
    }

    UInt32 numberOfArchivedItems = 0;
    if (inputArchive)
    {
        HRESULT hr = inputArchive->CanRead();
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error(L"Failed to read 'inputArchive' [{}]", ec);
            return;
        }

        hr = inputArchive->SetFilePointer(0LL, FILE_BEGIN, NULL);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error(L"Failed to seek input archive to the begining [{}]", ec);
            return;
        }

        CComQIPtr<IInArchive, &IID_IInArchive> archiverIn(archiver);
        CComPtr<InStreamAdapter> inStream = new InStreamAdapter(inputArchive);
        CComPtr<ArchiveOpenCallback> archiveOpenCallback(new ArchiveOpenCallback());
        hr = archiverIn->Open(inStream, nullptr, archiveOpenCallback);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error(L"Failed to open input archive [{}]", ec);
            return;
        }

        hr = archiverIn->GetNumberOfItems(&numberOfArchivedItems);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error(L"Failed to retrieve number of items [{}]", ec);
            return;
        }
    }

    const auto numberOfNewItems = m_items.size();

    CComPtr<ArchiveUpdateCallback> archiveUpdateCallback(
        new ArchiveUpdateCallback(std::move(m_items), m_password, numberOfArchivedItems));

    m_items.clear();

    CComQIPtr<ISequentialOutStream, &IID_ISequentialOutStream> outStream(new OutStreamAdapter(outputArchive));
    hr = archiver->UpdateItems(outStream, numberOfNewItems + numberOfArchivedItems, archiveUpdateCallback);
    if (FAILED(hr))
    {
        ec.assign(hr, std::system_category());
        Log::Error("Failed to update archive [{}]", ec);
        return;
    }

    // 'ec' will be set to an error if one of the intermediate item compression has failed
    ec = archiveUpdateCallback->GetCallbackError();
}

void Archive7z::Compress(const std::shared_ptr<ByteStream>& output, std::error_code& ec)
{
    Compress(output, {}, ec);
}

void Archive7z::SetCompressionLevel(Archive::CompressionLevel level, std::error_code& ec)
{
    m_compressionLevel = level;
}

const IArchive::Items& Archive7z::AddedItems() const
{
    return m_items;
};
