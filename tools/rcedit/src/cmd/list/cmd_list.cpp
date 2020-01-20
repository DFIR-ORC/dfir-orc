//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "cmd_list.h"

#include <spdlog/spdlog.h>

#include "cmd/validators.h"
#include "utils/iconv.h"

#include "resources.h"

using namespace rcedit;

using OptionsUtf8 = CmdList::OptionsUtf8;
using OptionsUtf16 = CmdList::OptionsUtf16;

namespace {

OptionsUtf16 ToUtf16( const OptionsUtf8& utf8, std::error_code& ec )
{
    OptionsUtf16 o;

    try {
        o.pePath = Utf8ToUtf16( utf8.pePath );
        o.type = Utf8ToUtf16( utf8.type );
    }
    catch( const std::system_error& e ) {
        ec = e.code();
        return {};
    }

    return o;
}

}  // namespace

namespace rcedit {

void CmdList::Register( CLI::App& app )
{
    auto& o = m_optionsUtf8;

    m_listApp = app.add_subcommand( "list", "List resources" );

    m_listApp->add_option( "pe_file_path", o.pePath, "Input PE file path" )
        ->required()
        ->check( CLI::ExistingFile );

    m_listApp->add_option( "-t,--type", o.type, "Resource type to list" )
        ->check( ValidateOptionTypeOrName );
}

void CmdList::Execute( std::error_code& ec )
{
    const auto& o = ToUtf16( m_optionsUtf8, ec );
    if( ec ) {
        spdlog::error( "Failed to convert utf-8 options to utf-16" );
        return;
    }

    Resources resources;
    resources.Load( o.pePath, ec );
    if( ec ) {
        spdlog::error( "Failed to load resources: {}", ec.message() );
        return;
    }

    resources.Print();
}

bool CmdList::Parsed() const
{
    return m_listApp->parsed();
}

}  // namespace rcedit
