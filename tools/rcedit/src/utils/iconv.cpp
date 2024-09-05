//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "utils/iconv.h"

#include <windows.h>
#include <assert.h>

namespace rcedit {

std::string Utf16ToUtf8( const std::wstring& utf16, std::error_code& ec )
{
    if( utf16.empty() ) {
        return {};
    }

    //
    // From MSDN:
    //
    // If this parameter is -1, the function processes the entire input string,
    // including the terminating null character. Therefore, the resulting
    // character string has a terminating null character, and the length
    // returned by the function includes this character.
    //
    const auto requiredSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.c_str(),
        static_cast< int >( utf16.size() ),
        NULL,
        0,
        NULL,
        NULL );

    if( requiredSize == 0 ) {
        ec.assign( GetLastError(), std::system_category() );
        return {};
    }

    std::string utf8;
    utf8.resize( requiredSize );

    const auto converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.c_str(),
        static_cast < int >( utf16.size() ),
        utf8.data(),
        static_cast< int >( utf8.size() ),
        NULL,
        NULL );

    if( converted == 0 ) {
        ec.assign( GetLastError(), std::system_category() );
        return {};
    }

    assert( converted == requiredSize );
    return utf8;
}

std::string Utf16ToUtf8( const std::wstring& utf16 )
{
    std::error_code ec;
    const auto utf8 = Utf16ToUtf8( utf16, ec );
    if( ec ) {
        throw std::system_error( ec, "Failed conversion from utf16 to utf8" );
    }

    return utf8;
}

std::wstring Utf8ToUtf16( const std::string& utf8, std::error_code& ec )
{
    if( utf8.empty() ) {
        return {};
    }

    //
    // From MSDN:
    //
    // If this parameter is -1, the function processes the entire input string,
    // including the terminating null character. Therefore, the resulting
    // character string has a terminating null character, and the length
    // returned by the function includes this character.
    //
    const auto requiredSize = MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast< int >( utf8.size() ), NULL, 0 );

    if( requiredSize == 0 ) {
        ec.assign( GetLastError(), std::system_category() );
        return {};
    }

    std::wstring utf16;
    utf16.resize( requiredSize );

    const auto converted = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.c_str(),
        static_cast< int >( utf8.size() ),
        utf16.data(),
        static_cast< int >( utf16.size() ) );

    if( converted == 0 ) {
        ec.assign( GetLastError(), std::system_category() );
        return {};
    }

    assert( converted == requiredSize );
    return utf16;
}

std::wstring Utf8ToUtf16( const std::string& utf8 )
{
    std::error_code ec;
    const auto utf16 = Utf8ToUtf16( utf8, ec );
    if( ec ) {
        throw std::system_error( ec, "Failed conversion from utf8 to utf16" );
    }

    return utf16;
}

std::optional< std::wstring > Utf8ToUtf16(
    const std::optional< std::string >& utf8 )
{
    if( utf8.has_value() == false ) {
        return {};
    }

    return Utf8ToUtf16( *utf8 );
}

}  // namespace rcedit
