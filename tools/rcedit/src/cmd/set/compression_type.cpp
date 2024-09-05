//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "cmd/set/compression_type.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#include <spdlog/spdlog.h>

namespace rcedit {

std::string ToString( CompressionType type )
{
    switch( type ) {
        case CompressionType::k7zip:
            return "7z";
        case CompressionType::kNone:
            return "none";
        default:
            return "<unsupported_compression_type>";
    }
}

CompressionType ToCompressionType( const std::string& value )
{
    const std::map< std::string, CompressionType > toEnumClass = {
        { "none", CompressionType::kNone }, { "7z", CompressionType::k7zip }
    };

    std::string type;
    std::transform(
        std::cbegin( value ),
        std::cend( value ),
        std::back_inserter( type ),
        []( unsigned char c ) { return std::tolower( c ); } );

    try {
        return toEnumClass.at( type );
    }
    catch( const std::exception& ) {
        spdlog::error(
            "Failed to convert '{}' to a known compression type", value );
        throw;
    }
}

std::istream& operator>>( std::istream& in, CompressionType& type )
{
    std::string s;
    in >> s;
    type = ToCompressionType( s );
    return in;
}

std::istream& operator>>(
    std::istream& in,
    std::optional< CompressionType >& type )
{
    if( !type ) {
        return in;
    }

    return operator>>( in, *type );
}

}  // namespace rcedit
