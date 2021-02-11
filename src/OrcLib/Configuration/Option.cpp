//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Option.h"

#include <fmt/core.h>

#include <boost/algorithm/string.hpp>

using namespace Orc;

namespace {

bool SplitArgumentAndSubArguments(std::wstring_view input, std::wstring_view& argument, std::wstring_view& optionString)
{
    const auto subOptionsPos = input.find_first_of(L':');
    if (subOptionsPos == std::wstring::npos)
    {
        return false;
    }

    argument = std::wstring_view(input.data(), subOptionsPos);
    optionString = std::wstring_view(input.data() + subOptionsPos + 1);
    return true;
}

}  // namespace

namespace Orc {

void ToOptions(std::wstring_view optionString, std::vector<Option>& options)
{
    std::vector<std::wstring> items;
    boost::split(items, optionString, boost::is_any_of(L","));

    for (const auto& item : items)
    {
        const auto separatorPos = item.find_first_of(L'=');
        if (separatorPos == std::wstring::npos)
        {
            options.push_back({item, {}});
            continue;
        }

        std::wstring key(item.data(), separatorPos);
        std::wstring value(item.data() + separatorPos + 1);
        options.push_back({boost::to_lower_copy(key), std::move(value)});
    }
}

bool ParseSubArguments(std::wstring_view input, std::wstring_view argument, std::vector<Option>& options)
{
    std::wstring_view inputArgument;
    std::wstring_view subArguments;

    if (!SplitArgumentAndSubArguments(input, inputArgument, subArguments))
    {
        return false;
    }

    if (!boost::iequals(inputArgument, argument))
    {
        return false;
    }

    ::ToOptions(subArguments, options);
    return true;
}

std::wstring Join(
    const std::vector<Option>& options,
    const std::wstring& prefix,
    const std::wstring& suffix,
    const std::wstring& separator)
{
    std::vector<std::wstring> strings;

    for (const auto& option : options)
    {
        if (option.value)
        {
            strings.emplace_back(fmt::format(L"{}{}={}{}", prefix, option.key, *option.value, suffix));
        }
        else
        {
            strings.emplace_back(fmt::format(L"{}{}{}", prefix, option.key, suffix));
        }
    }

    return boost::join(strings, separator);
}

}  // namespace Orc
