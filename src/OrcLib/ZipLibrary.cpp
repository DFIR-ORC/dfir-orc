//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ZipLibrary.h"

#include <boost/algorithm/string.hpp>

#include <7zip/Archive/IArchive.h>
#include <7zip/ICoder.h>
#include <7zip/7zip.h>
#include <7zip/extras.h>

#include "LogFileWriter.h"
#include "PropVariant.h"

using namespace Orc;

namespace {

#ifdef _7ZIP_STATIC
// Static linking requires calling specifics functions.
// Call '::Lib7z::Instance()' to initialize singleton
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

        NCrypto::N7z::RegisterCodec7zAES();
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

bool hasExtension(const Orc::ZipLibrary::ArchiveFormat& format, const std::wstring& extension)
{
    const auto result =
        std::find_if(std::cbegin(format.Extensions), std::cend(format.Extensions), [&extension](const auto& other) {
            return boost::iequals(extension, other);
        });
    return result != std::cend(format.Extensions);
}

std::optional<Orc::ZipLibrary::ArchiveFormat>
Find(const std::vector<Orc::ZipLibrary::ArchiveFormat>& formats, const std::wstring& extension)
{
    const auto result = std::find_if(std::cbegin(formats), std::cend(formats), [&extension](const auto& format) {
        return hasExtension(format, extension);
    });

    if (result == std::cend(formats))
    {
        return {};
    }

    return *result;
}
}  // namespace

std::unique_ptr<ZipLibrary> ZipLibrary::CreateZipLibrary(logger log)
{
    auto zip = std::unique_ptr<ZipLibrary>(new ZipLibrary(std::move(log)));
    if (FAILED(zip->Initialize()))
        return nullptr;

    return zip;
}

std::shared_ptr<ZipLibrary> ZipLibrary::GetZipLibrary(logger log)
{
    static std::weak_ptr<ZipLibrary> singleton;

    auto lib = singleton.lock();
    if (lib)
    {
        return lib;
    }

    lib = CreateZipLibrary(std::move(log));
    if (lib == nullptr)
    {
        return nullptr;
    }

    singleton = lib;
    return lib;
}

ZipLibrary::ZipLibrary(std::shared_ptr<LogFileWriter> log)
    : _L_(std::move(log))
{
}

ZipLibrary::~ZipLibrary() = default;

HRESULT ZipLibrary::Initialize()
{
    HRESULT hr = E_FAIL;

#ifdef _7ZIP_STATIC
    ::Lib7z::Instance();
#endif  // _7ZIP_STATIC

    if (FAILED(hr = GetAvailableFormats(m_Formats)))
        return hr;

    if (FAILED(hr = GetAvailableCodecs(m_Codecs)))
        return hr;

    return S_OK;
}

HRESULT ZipLibrary::CreateObject(const GUID* clsID, const GUID* iid, void** outObject)
{
    return ::CreateObject(clsID, iid, outObject);
}

HRESULT ZipLibrary::GetAvailableFormats(std::vector<ArchiveFormat>& formats) const
{
    HRESULT hr = E_FAIL;

    UInt32 numFormats = 1;
    if (FAILED(hr = ::GetNumberOfFormats(&numFormats)))
    {
        log::Error(_L_, hr, L"Failed to get number of format functions\r\n");
        return hr;
    }

    for (size_t i = 0; i < numFormats; i++)
    {
        NWindows::NCOM::CPropVariant prop;
        ArchiveFormat fmt;

        if (FAILED(hr = ::GetHandlerProperty2((UInt32)i, NArchive::NHandlerPropID::kUpdate, &prop)))
        {
            log::Error(_L_, hr, L"Failed to get handler of property func kUpdate\r\n");
            return hr;
        }
        fmt.UpdateCapable = prop.boolVal ? true : false;

        if (FAILED(hr = ::GetHandlerProperty2((UInt32)i, NArchive::NHandlerPropID::kName, &prop)))
        {
            log::Error(_L_, hr, L"Failed to get handler of property func kName\r\n");
            return hr;
        }

        if (prop.bstrVal)
        {
            fmt.Name = prop.bstrVal;
        }

        if (FAILED(hr = ::GetHandlerProperty2((UInt32)i, NArchive::NHandlerPropID::kExtension, &prop)))
        {
            log::Error(_L_, hr, L"Failed to get handler of property func kExtension\r\n");
            return hr;
        }

        std::wstring extensions;
        if (prop.bstrVal)
        {
            extensions = prop.bstrVal;
            boost::split(fmt.Extensions, extensions, boost::is_any_of(L" "));
        }

        if (FAILED(hr = ::GetHandlerProperty2((UInt32)i, NArchive::NHandlerPropID::kClassID, &prop)))
        {
            log::Error(_L_, hr, L"Failed to get handler of property func kClassID\r\n");
            return hr;
        }

        if (prop.bstrVal)
        {
            fmt.ID = *((GUID*)prop.bstrVal);
        }

        formats.push_back(fmt);
    }

    return S_OK;
}

HRESULT ZipLibrary::GetAvailableCodecs(std::vector<ArchiveCodec>& codecs) const
{
    HRESULT hr = E_FAIL;

    UInt32 numMethods = 1;
    if (FAILED(hr = ::GetNumberOfMethods(&numMethods)))
    {
        return hr;
    }

    for (size_t i = 0; i < numMethods; i++)
    {
        ArchiveCodec codec;
        NWindows::NCOM::CPropVariant prop;

        if (FAILED(hr = ::GetMethodProperty((UInt32)i, NMethodPropID::kName, &prop)))
        {
            log::Error(_L_, hr, L"Failed to get method of property func kName\r\n");
            return hr;
        }

        if (prop.bstrVal)
        {
            codec.Name = prop.bstrVal;
        }

        if (FAILED(hr = ::GetMethodProperty((UInt32)i, NMethodPropID::kID, &prop)))
        {
            log::Error(_L_, hr, L"Failed to get method of property func kID\r\n");
            return hr;
        }

        codecs.push_back(codec);
    }

    return S_OK;
}

const GUID ZipLibrary::GetFormatCLSID(const std::wstring& extension) const
{
    const auto format = Find(m_Formats, extension);
    if (format)
    {
        return format->ID;
    }

    return CLSID_NULL;
}
