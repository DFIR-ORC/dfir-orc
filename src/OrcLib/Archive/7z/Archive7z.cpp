//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
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
    Lib7z(const Lib7z&) = delete;
    Lib7z& operator=(const Lib7z&) = delete;

    Lib7z()
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

        NCrypto::N7z::RegisterCodecSzAES();
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

// 7-Zip 24.09 raised the default LZMA/LZMA2 dictionary sizes (64-bit: Normal 16->32 MiB,
// Maximum 32->128 MiB, Ultra 64->256 MiB). As the LZMA encoder needs roughly 10.5x the dictionary
// per thread, this multiplied compression memory usage after the bundled library was upgraded from
// 22.01. Pin the dictionary to the pre-24.09 sizes so the footprint no longer depends on the 7-Zip
// version. Levels below 'Normal' were left unchanged by 24.09 and keep the library default.
// Returns nullptr when no override is needed.
const wchar_t* ToLib7zDictionarySize(Lib7zCompressionLevel level)
{
    switch (level)
    {
        case Lib7zCompressionLevel::Normal:
            return L"16m";
        case Lib7zCompressionLevel::Maximum:
            return L"32m";
        case Lib7zCompressionLevel::Ultra:
            return L"64m";
        default:
            return nullptr;
    }
}

UInt32 GetProcessorCount()
{
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
}

// Cap the LZMA2 thread count so peak compression memory no longer scales with the host core count.
// The encoder splits 'mt' as (block-threads x match-finder-threads); bt4 (Normal and above) uses 2
// match-finder threads, so total mt = 2 x block-threads, and block-threads is what multiplies memory.
// Use a single solid block (mt=2) on all architectures: this matches the pre-2024 GetThis behaviour
// (one block, not cores/2) and keeps peak memory close to a single dictionary. Bounded by the
// available processors. Returns 0 to keep the library default (one thread per processor).
UInt32 ToLib7zThreadCount(Lib7zCompressionLevel level)
{
    switch (level)
    {
        case Lib7zCompressionLevel::Normal:
        case Lib7zCompressionLevel::Maximum:
        case Lib7zCompressionLevel::Ultra:
        {
            const UInt32 cap = 2u;
            const UInt32 cores = GetProcessorCount();
            return cores < cap ? cores : cap;
        }
        default:
            return 0;
    }
}

void SetCompressionLevel(
    const CComPtr<IOutArchive>& archiver,
    Archive::Format format,
    CompressionLevel level,
    std::error_code& ec)
{
    Log::Debug("Archive7z: SetCompressionLevel to {}", ToString(level));

    CMyComPtr<ISetProperties> setProperties;
    HRESULT hr = archiver->QueryInterface(IID_ISetProperties, (void**)&setProperties);
    if (setProperties == nullptr)
    {
        ec.assign(hr, std::system_category());
        Log::Error("Failed to set compression level on IID_ISetProperties [{}]", ec);
        return;
    }

    const auto lib7zLevel = ::ToLib7zLevel(level);

    // The dictionary and thread overrides only apply to the LZMA2-based 7z format. The Zip format
    // uses Deflate (fixed 32 KiB window) and would not benefit from (and could reject) the 'd' property.
    const bool is7z = (format == Archive::Format::k7z);
    const wchar_t* dictionarySize = is7z ? ::ToLib7zDictionarySize(lib7zLevel) : nullptr;
    const UInt32 threadCount = is7z ? ::ToLib7zThreadCount(lib7zLevel) : 0;

    const wchar_t* names[3];
    NWindows::NCOM::CPropVariant values[3];
    UInt32 numProps = 0;

    names[numProps] = L"x";
    values[numProps] = static_cast<UINT32>(lib7zLevel);
    ++numProps;

    if (dictionarySize)
    {
        names[numProps] = L"d";
        values[numProps] = dictionarySize;
        ++numProps;
    }

    if (threadCount)
    {
        names[numProps] = L"mt";
        values[numProps] = static_cast<UINT32>(threadCount);
        ++numProps;
    }

    hr = setProperties->SetProperties(names, values, numProps);
    if (FAILED(hr))
    {
        ec.assign(hr, std::system_category());
        Log::Error("Failed to change compression level while setting property [{}]", ec);
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

    ::SetCompressionLevel(archiver, m_format, m_compressionLevel, ec);
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
        CComPtr<InStreamAdapter> inStream = new InStreamAdapter(inputArchive, true);
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
