//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include <optional>
#include <string>

#include <windows.h>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "utils/iconv.h"
#include "cmd/extract/cmd_extract.h"
#include "cmd/list/cmd_list.h"
#include "cmd/set/cmd_set.h"

namespace fs = std::filesystem;

namespace rcedit {

int main( const std::vector< std::string >& args )
{
    CLI::App app{
        "Command line tool to edit resources of exe file on Windows."
    };

    app.require_subcommand( 1 );

    // TODO: add option to set log verbose/debug
    spdlog::set_level( spdlog::level::info );

    rcedit::CmdList cmdList;
    cmdList.Register( app );

    rcedit::CmdSet cmdSet;
    cmdSet.Register( app );

    rcedit::CmdExtract cmdExtract;
    cmdExtract.Register( app );

    try {
        std::vector< const char* > argv;
        for( const auto& arg : args ) {
            argv.push_back( arg.data() );
        }

        app.parse( static_cast< int >( argv.size() ), &argv[ 0 ] );
    }
    catch( const CLI::ParseError& e ) {
        spdlog::error( "Parse error: {}", e.what() );
        return app.exit( e );
    }

    std::error_code ec;
    if( cmdList.Parsed() ) {
        cmdList.Execute( ec );
    }
    else if( cmdSet.Parsed() ) {
        cmdSet.Execute( ec );
    }
    else if( cmdExtract.Parsed() ) {
        cmdExtract.Execute( ec );
    }

    if( ec ) {
        spdlog::error( "Failed to set resource" );
        ExitProcess( EXIT_FAILURE );
    }

    return 0;
}

}  // namespace rcedit

int wmain( int argc, const wchar_t* const argv[] )
{
    try {
        // Convert arguments to utf8 for compatibility with CLI11
        std::vector< std::string > args;
        for( int i = 0; i < argc; ++i ) {
            args.push_back( std::move( rcedit::Utf16ToUtf8( argv[ i ] ) ) );
        }

        rcedit::main( args );
    }
    catch( const std::system_error& e ) {
        const auto& ec = e.code();
        spdlog::error( "Exception caught: {}", e.what() );
        return ec.value();
    }
    catch( const std::exception& e ) {
        spdlog::error( "Exception caught: {}", e.what() );
        return EXIT_FAILURE;
    }
}
