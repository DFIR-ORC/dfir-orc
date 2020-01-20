//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "cmd_set.h"

#include <fstream>
#include <filesystem>
#include <string_view>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "cmd/validators.h"
#include "cmd/set/compression_type.h"
#include "cmd/set/7z/compress_7z.h"
#include "utils/guard.h"
#include "utils/iconv.h"

using namespace rcedit;

namespace fs = std::filesystem;

using OptionsUtf8 = CmdSet::OptionsUtf8;
using OptionsUtf16 = CmdSet::OptionsUtf16;

namespace {

void UpdateFileResource(
    const fs::path& path,
    const std::wstring& type,
    const std::wstring& name,
    const std::optional< std::wstring >& lang,
    const std::vector< uint8_t >& value,
    std::error_code& ec )
{
    BOOL ret = FALSE;

    guard::BeginUpdateResourceHandleW hUpdate =
        BeginUpdateResourceW( path.c_str(), FALSE );

    if( !hUpdate ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed BeginUpdateResource: {}", ec.message() );
        return;
    }

    const auto langId =
        lang ? std::stoi( *lang ) : MAKELANGID( LANG_NEUTRAL, SUBLANG_NEUTRAL );

    ret = UpdateResourceW(
        hUpdate.get(),
        type.c_str(),
        name.c_str(),
        langId,
        reinterpret_cast< LPVOID >( const_cast< uint8_t* >( value.data() ) ),
        static_cast< DWORD >( value.size() ) );

    if( ret == FALSE ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed UpdateResource: {}", ec.message() );
        return;
    }

    ret = EndUpdateResourceW( hUpdate.get(), FALSE );

    if( ret == FALSE ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed EndUpdateResource: {}", ec.message() );
        return;
    }

    hUpdate.detach();
}

std::pair< std::string_view, guard::MapView > MapFile(
    const fs::path filePath,
    std::error_code& ec )
{
    BOOL ret = FALSE;

    guard::FileHandle hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL );

    if( !hFile ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed to open file: {}", ec.message() );
        return {};
    }

    LARGE_INTEGER fileSize;
    ret = GetFileSizeEx( hFile.get(), &fileSize );
    if( ret == FALSE || fileSize.HighPart != 0 ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed to get file size: {}", ec.message() );
        return {};
    }

    const auto name = L"Local\\map_" + filePath.filename().wstring();
    guard::Handle hMap = CreateFileMappingW(
        hFile.get(), NULL, PAGE_READONLY, 0, 0, name.c_str() );

    if( !hMap ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed to create mapping: {}", ec.message() );
        return {};
    }

    guard::MapView hView =
        MapViewOfFile( hMap.get(), FILE_MAP_READ | FILE_MAP_COPY, 0, 0, 0 );

    if( !hView ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed to map file: {}", ec.message() );
        return {};
    }

    std::string_view view(
        reinterpret_cast< const char* >( hView.get() ), fileSize.LowPart );

    return { view, std::move( hView ) };
}

std::vector< uint8_t > Compress(
    CompressionType type,
    const std::vector< uint8_t >& input,
    const std::wstring& fileNameInArchive,
    std::error_code& ec )
{
    switch( type ) {
        case CompressionType::kNone:
            return input;

        case CompressionType::k7zip: {
            std::vector< uint8_t > out7z;

            Compress7z( input, fileNameInArchive, out7z, ec );
            if( ec ) {
                spdlog::debug( "Failed to compress: {}", ec.message() );
                return {};
            }

            return out7z;
        }

        default:
            ec.assign( ERROR_INVALID_PARAMETER, std::system_category() );
            spdlog::debug( "Failed to update resource" );
            return {};
    }

    return {};
}

std::vector< uint8_t > ConvertResValueOption(
    const std::optional< std::string >& resUtf8,
    const std::optional< std::wstring >& resUtf16,
    const std::optional< std::wstring >& resPath,
    std::error_code& ec )
{
    std::vector< uint8_t > value;
    if( resUtf8 ) {
        std::copy(
            std::cbegin( *resUtf8 ),
            std::cend( *resUtf8 ),
            std::back_inserter( value ) );
    }
    else if( resUtf16 ) {
        std::string_view utf16(
            reinterpret_cast< const char* >( resUtf16->data() ),
            resUtf16->size() * sizeof( wchar_t ) );

        std::copy(
            std::cbegin( utf16 ),
            std::cend( utf16 ),
            std::back_inserter( value ) );
    }
    else if( resPath ) {
        const auto [ view, hView ] = MapFile( *resPath, ec );
        if( ec ) {
            spdlog::debug( "Failed to map resource" );
            return {};
        }

        std::copy(
            std::cbegin( view ),
            std::cend( view ),
            std::back_inserter( value ) );
    }
    else {
        ec.assign( ERROR_INVALID_PARAMETER, std::system_category() );
        return {};
    }

    return value;
}

template < typename T >
void Dump( const T& container, const fs::path& outputPath )
{
    std::ofstream ofs( outputPath, std::ios_base::binary );
    std::copy(
        std::cbegin( container ),
        std::cend( container ),
        std::ostreambuf_iterator< char >( ofs ) );
}

}  // namespace

namespace rcedit {

OptionsUtf16 ToUtf16( const OptionsUtf8& utf8, std::error_code& ec )
{
    OptionsUtf16 o;

    try {
        o.pePath = Utf8ToUtf16( utf8.pePath );
        o.type = Utf8ToUtf16( utf8.type );
        o.name = Utf8ToUtf16( utf8.name );
        o.lang = Utf8ToUtf16( utf8.lang );
        o.valueString = Utf8ToUtf16( utf8.valueString );
        o.valueUtf16 = Utf8ToUtf16( utf8.valueUtf16 );
        o.valuePath = Utf8ToUtf16( utf8.valuePath );
        o.outPath = Utf8ToUtf16( utf8.outPath );
        o.compression = utf8.compression;
    }
    catch( const std::system_error& e ) {
        ec = e.code();
        return {};
    }

    return o;
}

void CmdSet::Register( CLI::App& app )
{
    auto& o = m_optionsUtf8;

    m_setApp = app.add_subcommand( "set", "Add/Update a resource" );

    m_setApp->add_option( "pe_file_path", o.pePath, "Input PE file path" )
        ->required()
        ->check( CLI::ExistingFile );

    m_setApp->add_option( "-t,--type", o.type, "Resource type to set" )
        ->required()
        ->check( ValidateOptionTypeOrName );

    m_setApp->add_option( "-n,--name", o.name, "Resource name to set" )
        ->required()
        ->check( ValidateOptionTypeOrName );

    m_setApp->add_option( "-l,--lang", o.lang, "Resource lang to set" )
        ->check( ValidateOptionLang );

    auto resourceInputType =
        m_setApp->add_option_group( "input_type", "Resource input type" );
    resourceInputType->require_option( 1 );

    resourceInputType
        ->add_option(
            "--value-path", o.valuePath, "Resource to load from file" )
        ->check( CLI::ExistingFile );

    resourceInputType->add_option(
        "--value", o.valueString, "Resource value to store as utf-8" );

    resourceInputType->add_option(
        "--value-utf16", o.valueUtf16, "Resource value to store as utf-16" );

    std::vector< std::pair< std::string, CompressionType > > map{
        { "no", CompressionType::kNone }, { "7z", CompressionType::k7zip }
    };
    m_setApp
        ->add_option(
            "-c,--compress", o.compression, "Use compression algorithm" )
        ->transform( CLI::CheckedTransformer( map, CLI::ignore_case ) );

    m_setApp->add_option( "-o,--out", o.outPath, "Output file path" );
}

void CmdSet::Execute( std::error_code& ec )
{
    const auto& o = ToUtf16( m_optionsUtf8, ec );
    if( ec ) {
        spdlog::error( "Failed to convert utf-8 options to utf-16" );
        return;
    }

    const fs::path outPath = o.outPath.value_or( o.pePath );
    if( o.outPath ) {
        fs::copy( o.pePath, outPath, fs::copy_options::overwrite_existing, ec );
        if( ec ) {
            spdlog::error( "Failed to copy file: {}", ec.message() );
            return;
        }
    }

    auto value = ConvertResValueOption(
        m_optionsUtf8.valueString, o.valueUtf16, o.valuePath, ec );

    if( o.compression && *o.compression != CompressionType::kNone ) {
        // TODO: make a compress option to override default name
        fs::path fileNameInArchive;
        if( o.valuePath ) {
            fileNameInArchive = fs::path( *o.valuePath ).filename();
        }
        else {
            fileNameInArchive = m_optionsUtf8.name;
        }

        value = Compress( *o.compression, value, fileNameInArchive, ec );
        if( ec ) {
            spdlog::error( "Failed to compress resource" );
            return;
        }
    }

    ::UpdateFileResource( outPath, o.type, o.name, o.lang, value, ec );
    if( ec ) {
        spdlog::error( "Failed to update resource" );
        return;
    }

    spdlog::info( "Updated resources: {}", outPath.string() );
}

bool CmdSet::Parsed() const
{
    return m_setApp->parsed();
}

}  // namespace rcedit
