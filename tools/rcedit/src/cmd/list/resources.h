//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <system_error>

namespace rcedit {

class Resources
{
public:
    struct ResourceLangDir
    {
        ResourceLangDir( uint16_t l )
            : lang( l )
        {
        }

        uint16_t lang;
    };

    struct ResourceNameDir
    {
        ResourceNameDir( const std::wstring& n )
            : name( n )
        {
        }

        std::wstring name;
        std::vector< ResourceLangDir > langs;
    };

    struct ResourceTypeDir
    {
        ResourceTypeDir( const std::wstring& t )
            : type( t )
        {
        }

        std::wstring type;
        std::vector< ResourceNameDir > names;
    };

    Resources() = default;
    virtual ~Resources();

    void Load( const std::filesystem::path& peFilePath, std::error_code& ec );
    void Print() const;

private:
    std::vector< ResourceTypeDir > m_types;
};

}  // namespace rcedit
