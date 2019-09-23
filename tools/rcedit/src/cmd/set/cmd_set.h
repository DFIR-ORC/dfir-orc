//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include "cmd/cmd.h"

#include <optional>
#include <string>

#include "cmd/set/compression_type.h"

namespace rcedit {

class CmdSet : public Cmd
{
public:
    // CLI11 only support utf8 so it must be available on registration even if
    // command will internally use utf16
    template < typename T >
    struct Options
    {
        T pePath;
        T type;
        T name;
        std::optional< T > lang;
        std::optional< T > valueString;
        std::optional< T > valueUtf16;
        std::optional< T > valuePath;
        std::optional< T > outPath;
        std::optional< CompressionType > compression;
    };

    using OptionsUtf8 = Options< std::string >;
    using OptionsUtf16 = Options< std::wstring >;

    void Register( CLI::App& app ) override;
    void Execute( std::error_code& ec ) override;
    bool Parsed() const override;

private:
    OptionsUtf8 m_optionsUtf8;
    CLI::App* m_setApp;
};

}  // namespace rcedit
