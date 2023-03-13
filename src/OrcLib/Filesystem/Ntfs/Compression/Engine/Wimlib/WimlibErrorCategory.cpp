//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "WimlibErrorCategory.h"

#include <string>
#include <system_error>

#include "Filesystem/Ntfs/Compression/Engine/Wimlib/WimlibApi.h"
#include "Text/iconv.h"

namespace Orc {

const wimlib_error_category_t& wimlib_error_category()
{
    static wimlib_error_category_t category;
    return category;
}

const char* wimlib_error_category_t::name() const noexcept
{
    return "wimlib";
}

std::error_condition wimlib_error_category_t::default_error_condition(int c) const noexcept
{
    switch (static_cast<wimlib_errc>(c))
    {
        case wimlib_errc::WIMLIB_ERR_INVALID_PARAM:
            return std::make_error_condition(std::errc::invalid_argument);
        default:
            return std::error_condition(c, *this);
    }
}

bool wimlib_error_category_t::equivalent(const std::error_code& code, int condition) const noexcept
{
    return *this == code.category() && static_cast<int>(default_error_condition(code.value()).value()) == condition;
}

std::string wimlib_error_category_t::message(int ev) const noexcept
{
    const auto kDefault = "<Missing description>";

    std::error_code ec;

    const auto utf16 = wimlib_get_error_string(wimlib_errc(ev), ec);
    if (ec)
    {
        return kDefault;
    }

    const auto utf8 = ToUtf8(utf16, ec);
    if (ec)
    {
        return kDefault;
    }

    return utf8;
}

}  // namespace Orc
