//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "cmd/validators.h"

#include <algorithm>

namespace {

const std::string kInvalidArgument( "Invalid argument" );

bool IsNumber( const std::string& string )
{
    if( string.empty() )
        return false;

    return std::all_of( std::cbegin( string ), std::cend( string ), ::isdigit );
}

}  // namespace

namespace rcedit {

std::string ValidateOptionTypeOrName( const std::string& type )
{
    if( type.empty() || ( type[ 0 ] == '#' && type.size() == 1 ) ) {
        return kInvalidArgument;
    }

    if( type[ 0 ] == '#' ) {
        const std::string x( type.c_str() + 1 );

        if( IsNumber( x ) == false ) {
            return kInvalidArgument;
        }

        try {
            std::ignore = std::stoi( x );
        }
        catch( const std::exception& e ) {
            return e.what();
        }
    }

    return {};
}

std::string ValidateOptionLang( const std::string& lang )
{
    if( IsNumber( lang ) == false ) {
        return kInvalidArgument;
    }

    try {
        std::ignore = std::stoi( lang );
    }
    catch( const std::exception& e ) {
        return e.what();
    }

    return {};
}

}  // namespace rcedit
