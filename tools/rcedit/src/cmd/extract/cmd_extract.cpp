//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "cmd_extract.h"

#include <filesystem>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "cmd/validators.h"
#include "utils/guard.h"
#include "utils/iconv.h"

using namespace rcedit;

namespace fs = std::filesystem;

using OptionsUtf8 = CmdExtract::OptionsUtf8;
using OptionsUtf16 = CmdExtract::OptionsUtf16;

namespace {

OptionsUtf16 ToUtf16( const OptionsUtf8& utf8, std::error_code& ec )
{
    OptionsUtf16 o;

    try {
        o.pePath = Utf8ToUtf16( utf8.pePath );
        o.type = Utf8ToUtf16( utf8.type );
        o.name = Utf8ToUtf16( utf8.name );
        o.lang = Utf8ToUtf16( utf8.lang );
        o.outPath = Utf8ToUtf16( utf8.outPath );
    }
    catch( const std::system_error& e ) {
        ec = e.code();
        return {};
    }

    return o;
}

std::vector< uint8_t > ExtractResource(
    const fs::path& pePath,
    const std::wstring& type,
    const std::wstring& name,
    WORD lang,
    std::error_code& ec )
{

    guard::ModuleHandle module =
        LoadLibraryExW( pePath.c_str(), NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE );
    if( module == nullptr ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::error( "Failed to load resource file: {}", ec.message() );
        return {};
    }

    HRSRC hResInfo =
        FindResourceExW( module.get(), type.c_str(), name.c_str(), lang );
    if( hResInfo == nullptr ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::error( "Failed to find resource: {}", ec.message() );
        return {};
    }

    HGLOBAL hResData = LoadResource( module.get(), hResInfo );
    if( hResData == nullptr ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::error( "Failed to load resource: {}", ec.message() );
        return {};
    }

    LPVOID resource = LockResource( hResData );
    if( resource == nullptr ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::error( "Failed to lock resource: {}", ec.message() );
        return {};
    }

    DWORD resourceSize = SizeofResource( module.get(), hResInfo );
    if( resourceSize == 0 ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::error( "Failed to get resource size: {}", ec.message() );
        return {};
    }

    std::string_view view(
        reinterpret_cast< const char* >( resource ), resourceSize );

    std::vector< uint8_t > out;

    std::copy(
        std::cbegin( view ), std::cend( view ), std::back_inserter( out ) );

    return out;
}

}  // namespace

namespace rcedit {

void CmdExtract::Register( CLI::App& app )
{
    auto& o = m_optionsUtf8;

    // TODO: add option to extract all resources to an output directory OR make
    // a new extract_dir command ?
    m_extractApp = app.add_subcommand( "extract", "extract resources" );

    m_extractApp->add_option( "pe_file_path", o.pePath, "Input PE file path" )
        ->required()
        ->check( CLI::ExistingFile );

    m_extractApp->add_option( "-t,--type", o.type, "Resource type to extract" )
        ->check( ValidateOptionTypeOrName )
        ->required();

    m_extractApp->add_option( "-n,--name", o.name, "Resource name to extract" )
        ->check( ValidateOptionTypeOrName )
        ->required();

    m_extractApp->add_option( "-l,--lang", o.lang, "Resource lang to extract" )
        ->check( ValidateOptionLang );

    m_extractApp->add_option( "-o,--out", o.outPath, "Output file path" )
        ->required();
}

void CmdExtract::Execute( std::error_code& ec )
{
    const auto& o = ToUtf16( m_optionsUtf8, ec );
    if( ec ) {
        spdlog::error( "Failed to convert utf-8 options to utf-16" );
        return;
    }

    const auto lang = o.lang ? std::stoi( *o.lang )
                             : MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT );
    const auto resource =
        ExtractResource( o.pePath.c_str(), o.type, o.name, lang, ec );
    if( ec ) {
        spdlog::error( "Failed to extract resource" );
        return;
    }

    std::ofstream ofs( o.outPath.c_str(), std::ios_base::binary );

    std::copy(
        std::cbegin( resource ),
        std::cend( resource ),
        std::ostreambuf_iterator< char >( ofs ) );

    ofs.close();
    if( !ofs ) {
        spdlog::error( "Failed to write resource" );
        ec.assign( ERROR_CANTWRITE, std::system_category() );
        return;
    }
}

bool CmdExtract::Parsed() const
{
    return m_extractApp->parsed();
}

}  // namespace rcedit
