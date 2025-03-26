//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <system_error>

#include <CLI/CLI.hpp>

namespace rcedit {

class Cmd
{
public:
    virtual ~Cmd() {}
    virtual void Register( CLI::App& app ) = 0;
    virtual void Execute( std::error_code& ec ) = 0;
    virtual bool Parsed() const = 0;
};

}  // namespace rcedit
