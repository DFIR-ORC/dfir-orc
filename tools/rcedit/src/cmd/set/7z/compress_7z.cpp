//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
// TODO: compression debug: missing solid option ? do a ttd ?

#include "compress_7z.h"

#include <spdlog/spdlog.h>

#include <7zip/extras.h>

#include "utils/guard.h"

#include "archive_update_callback.h"
#include "in_mem_stream.h"
#include "out_mem_stream.h"

namespace fs = std::filesystem;

namespace {

#ifdef _7ZIP_STATIC

// Static linking requires calling specifics functions.
// Call '::Lib7z::Instance()' to initialize singleton

class Lib7z
{
    Lib7z::Lib7z( const Lib7z& ) = delete;
    Lib7z& Lib7z::operator=( const Lib7z& ) = delete;

    Lib7z::Lib7z()
    {
        ::lib7zCrcTableInit();
        NArchive::N7z::Register();
        NCompress::NBcj::RegisterCodecBCJ();
        NCompress::NBcj2::RegisterCodecBCJ2();
        NCompress::NLzma::RegisterCodecLZMA();
        NCompress::NLzma2::RegisterCodecLZMA2();
    }

public:
    static Lib7z& Instance()
    {
        static Lib7z lib;
        return lib;
    }
};

#endif  // _7ZIP_STATIC

void SetCompressionLevel(
    IOutArchive* outArchive,
    UInt32 level,
    std::error_code& ec )
{
    CMyComPtr< ISetProperties > setProperties;
    HRESULT hr = outArchive->QueryInterface(
        IID_ISetProperties, (void**)&setProperties );

    if( setProperties == nullptr ) {
        ec.assign( hr, std::system_category() );
        spdlog::warn( "Failed retrieve IID_ISetProperties: {}", ec.message() );
        return;
    }

    const uint8_t numProps = 1;
    const wchar_t* names[ numProps ] = { L"x" };
    NWindows::NCOM::CPropVariant values[ numProps ] = { level };

    hr = setProperties->SetProperties( names, values, numProps );
    if( FAILED( hr ) ) {
        ec.assign( hr, std::system_category() );
        spdlog::warn( "Failed to set property: {}", ec.message() );
        return;
    }
}

}  // namespace

namespace rcedit {

void Compress7z(
    const fs::path& inputFile,
    std::vector< uint8_t >& out,
    std::error_code& ec )
{
#ifdef _7ZIP_STATIC
    ::Lib7z::Instance();
#endif  // _7ZIP_STATIC

    guard::ComPtr< OutMemStream > outStream( new OutMemStream( out ) );
    guard::ComPtr< IOutArchive > outArchive;

    HRESULT hr = ::CreateObject(
        &CLSID_CFormat7z,
        &IID_IOutArchive,
        reinterpret_cast< void** >( &outArchive ) );

    if( FAILED( hr ) ) {
        ec.assign( hr, std::system_category() );
        spdlog::debug( "Failed to get IID_IOutArchive: {}", ec.message() );
        return;
    }

    SetCompressionLevel( outArchive.get(), 9, ec );
    if( ec ) {
        ec.assign( E_FAIL, std::system_category() );
        spdlog::debug( "Failed to update compression level" );
        return;
    }

    auto updateCb = ArchiveUpdateCallback::Create( inputFile, ec );
    if( ec ) {
        ec.assign( E_FAIL, std::system_category() );
        spdlog::debug( "Failed to instanciate ArchiveUpdateCallback" );
        return;
    }

    hr = outArchive->UpdateItems( outStream.get(), 1, updateCb.get() );
    if( FAILED( hr ) ) {
        ec.assign( hr, std::system_category() );
        spdlog::debug( "Failed to add file to archive: {}", ec.message() );
        return;
    }

    // 'UpdateItems' called 'updateCb' methods which could failed
    ec = updateCb->callbackError();
    if( ec ) {
        spdlog::debug( "Failed to compress: {}", ec.message() );
        return;
    }
}

void Compress7z(
    const std::string_view& input,
    const std::wstring& filename,
    std::vector< uint8_t >& out,
    std::error_code& ec )
{
#ifdef _7ZIP_STATIC
    ::Lib7z::Instance();
#endif  // _7ZIP_STATIC

    guard::ComPtr< OutMemStream > outStream( new OutMemStream( out ) );
    guard::ComPtr< IOutArchive > outArchive;

    HRESULT hr = ::CreateObject(
        &CLSID_CFormat7z,
        &IID_IOutArchive,
        reinterpret_cast< void** >( &outArchive ) );

    if( FAILED( hr ) ) {
        ec.assign( hr, std::system_category() );
        spdlog::debug( "Failed to get IID_IOutArchive: {}", ec.message() );
        return;
    }

    SetCompressionLevel( outArchive.get(), 9, ec );
    if( ec ) {
        ec.assign( E_FAIL, std::system_category() );
        spdlog::debug( "Failed to update compression level" );
        return;
    }

    auto updateCb = ArchiveUpdateCallback::Create( input, filename, ec );
    if( ec ) {
        ec.assign( E_FAIL, std::system_category() );
        spdlog::debug( "Failed to instanciate ArchiveUpdateCallback" );
        return;
    }

    hr = outArchive->UpdateItems( outStream.get(), 1, updateCb.get() );
    if( FAILED( hr ) ) {
        ec.assign( hr, std::system_category() );
        spdlog::debug( "Failed to add file to archive: {}", ec.message() );
        return;
    }

    // 'outArchive' calls 'updateCb' methods which could fail
    ec = updateCb->callbackError();
    if( ec ) {
        spdlog::debug( "Failed to compress: {}", ec.message() );
        return;
    }
}

void Compress7z(
    const std::vector< uint8_t >& input,
    const std::wstring& fileNameInArchive,
    std::vector< uint8_t >& out,
    std::error_code& ec )
{
    std::string_view view(
        reinterpret_cast< const char* >( input.data() ), input.size() );

    Compress7z( view, fileNameInArchive, out, ec );
}

}  // namespace rcedit
