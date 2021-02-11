//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <optional>
#include <vector>

namespace Orc {

struct Option
{
    Option(std::wstring_view k, std::optional<std::wstring_view> v)
        : key(k)
        , value(v)
        , isParsed(false)
    {
    }

    const std::wstring key;
    const std::optional<std::wstring> value;
    bool isParsed;
};

// Parse sub-options from 'arg' like '/log:file,output=foo.log,encoding=utf8' after checking that 'argument'
// matches input. Return true if parsing is succesfull.
bool ParseSubArguments(std::wstring_view input, std::wstring_view argument, std::vector<Option>& subOptions);

// Parse sub-options from strings like: 'file,output=foo.log,encoding=utf8' keeping them paired within 'Option' items
void ToOptions(std::wstring_view optionString, std::vector<Option>& options);

// Build an "option string" from input 'options' and return something like "file,output=foobar.log,encoding=utf8'
std::wstring Join(
    const std::vector<Option>& options,
    const std::wstring& prefix,
    const std::wstring& suffix,
    const std::wstring& separator);

}  // namespace Orc
