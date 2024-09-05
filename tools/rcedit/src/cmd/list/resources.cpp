//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "resources.h"

#include <iostream>
#include <map>

#include <windows.h>

#include <spdlog/spdlog.h>

#include "utils/guard.h"

namespace fs = std::filesystem;

namespace {

using ResourceTypeDir = rcedit::Resources::ResourceTypeDir;
using ResourceNameDir = rcedit::Resources::ResourceNameDir;
using ResourceLangDir = rcedit::Resources::ResourceLangDir;

std::wstring StringFromResourceId( LPCWSTR resId )
{
    const std::wstring kNaN( L"#NaN" );

    if( resId == nullptr ) {
        return kNaN;
    }

    // TODO: did not found a size limit on resource size
    if( IS_INTRESOURCE( resId ) == FALSE ) {
        return resId;
    }

#pragma warning( push )
#pragma warning( disable : 4302 )
    const WORD id = reinterpret_cast<size_t>(resId) & 0xFFFF;
#pragma warning( pop )

    try {
        return L"#" + std::to_wstring( id );
    }
    catch( const std::exception& e ) {
        spdlog::debug( "Failed to convert resource id: {}", e.what() );
        return kNaN;
    }
}

BOOL CALLBACK EnumResourceTypeCb( HMODULE, LPWSTR lpType, LONG_PTR lParam )
{
    auto types = reinterpret_cast< std::vector< ResourceTypeDir >* >( lParam );
    auto type = StringFromResourceId( lpType );
    types->push_back( std::move( type ) );
    return TRUE;
}

BOOL GetResourceTypes( HMODULE hModule, std::vector< ResourceTypeDir >& types )
{
    return EnumResourceTypesW(
        hModule, &EnumResourceTypeCb, reinterpret_cast< LONG_PTR >( &types ) );
}

BOOL CALLBACK
EnumResourceNamesCb( HMODULE, LPCWSTR type, LPWSTR lpName, LONG_PTR lParam )
{
    auto names = reinterpret_cast< std::vector< ResourceNameDir >* >( lParam );

    auto name = StringFromResourceId( lpName );
    names->push_back( std::move( name ) );
    return TRUE;
}

BOOL GetResourceNames(
    HMODULE hModule,
    const std::wstring& type,
    std::vector< ResourceNameDir >& names )
{
    return EnumResourceNamesW(
        hModule,
        type.c_str(),
        &EnumResourceNamesCb,
        reinterpret_cast< LONG_PTR >( &names ) );
}

BOOL CALLBACK EnumResourceLanguageCb(
    HMODULE,
    LPCWSTR type,
    LPCWSTR name,
    WORD wLang,
    LONG_PTR lParam )
{
    auto langs = reinterpret_cast< std::vector< ResourceLangDir >* >( lParam );

    langs->emplace_back( wLang );
    return TRUE;
}

BOOL GetResourceLang(
    HMODULE hModule,
    const std::wstring& type,
    const std::wstring& name,
    std::vector< ResourceLangDir >& langs )
{
    return EnumResourceLanguagesW(
        hModule,
        type.c_str(),
        name.c_str(),
        &EnumResourceLanguageCb,
        reinterpret_cast< LONG_PTR >( &langs ) );
}

std::wstring GetFriendlyType( const std::wstring& type )
{
    using ResourceTypeDir = std::map< std::wstring, std::wstring >;

    // TODO: one line per entry
    const ResourceTypeDir ResourceTypeDirs = {
        { L"#1", L"RT_CURSOR" },        { L"#2", L"RT_BITMAP" },
        { L"#3", L"RT_ICON" },          { L"#4", L"RT_MENU" },
        { L"#5", L"RT_DIALOG" },        { L"#6", L"RT_STRING" },
        { L"#7", L"RT_FONTDIR" },       { L"#8", L"RT_FONT" },
        { L"#9", L"RT_ACCELERATOR" },   { L"#10", L"RT_RCDATA" },
        { L"#11", L"RT_MESSAGETABLE" }, { L"#16", L"RT_VERSION" },
        { L"#17", L"RT_DLGINCLUDE" },   { L"#19", L"RT_PLUGPLAY" },
        { L"#20", L"RT_VXD" },          { L"#21", L"RT_ANICURSOR" },
        { L"#22", L"RT_ANIICON" },      { L"#23", L"RT_HTML" },
        { L"#24", L"RT_MANIFEST" }
    };

    ResourceTypeDir::const_iterator it;
    if( ( it = ResourceTypeDirs.find( type ) )
        == std::cend( ResourceTypeDirs ) ) {
        return type;
    }

    return type + L" (" + it->second + L")";
}

}  // namespace

namespace rcedit {

Resources::~Resources() = default;

void Resources::Load( const fs::path& peFilePath, std::error_code& ec )
{
    BOOL ret = FALSE;

    m_types.clear();

    guard::ModuleHandle module = LoadLibraryExW(
        peFilePath.wstring().c_str(), NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE );
    if( module == nullptr ) {
        ec.assign( GetLastError(), std::system_category() );
        spdlog::debug( "Failed to open: {}", ec.message() );
        return;
    }

    ret = ::GetResourceTypes( module.get(), m_types );
    if( ret == FALSE ) {
        spdlog::warn( "Failed enumerating rsrc types: {}", GetLastError() );
    }

    for( auto& typeDir : m_types ) {
        const auto& type = typeDir.type;
        ret = ::GetResourceNames( module.get(), type, typeDir.names );
        if( ret == FALSE ) {
            spdlog::warn( "Failed enumerating rsrc name: {}", GetLastError() );
        }

        for( auto& nameDir : typeDir.names ) {
            const auto& name = nameDir.name;
            ret = ::GetResourceLang( module.get(), type, name, nameDir.langs );
            if( ret == FALSE ) {
                spdlog::warn(
                    "Failed enumerating rsrc lang: {}", GetLastError() );
            }
        }
    }
}

void Resources::Print() const
{
    // tuple< type, name, lang >
    using ResourcePath = std::tuple< std::wstring, std::wstring, std::wstring >;
    std::vector< ResourcePath > paths;

    for( const auto& typeDir : m_types ) {
        ResourcePath path;

        std::get< 0 >( path ) = typeDir.type;

        for( const auto& nameDir : typeDir.names ) {
            std::get< 1 >( path ) = nameDir.name;

            for( const auto& langDir : nameDir.langs ) {
                std::get< 2 >( path ) = std::to_wstring( langDir.lang );
                paths.push_back( path );
            }
        }
    }

    const auto kWidth = 24;

    std::wcout << std::left << std::setw( kWidth ) << "Type"
               << std::setw( kWidth ) << "Name" << std::setw( kWidth ) << "Lang"
               << std::endl;

    std::wcout << std::wstring( 64, L'-' ) << std::endl;

    for( const auto& path : paths ) {
        std::wcout << std::left << std::setw( kWidth )
                   << GetFriendlyType( std::get< 0 >( path ) )
                   << std::setw( kWidth ) << std::get< 1 >( path )
                   << std::setw( kWidth ) << std::get< 2 >( path ) << std::endl;
    }
}

}  // namespace rcedit
