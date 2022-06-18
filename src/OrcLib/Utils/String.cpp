//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "String.h"

#include <boost/algorithm/string/predicate.hpp>

namespace {

template <typename T>
bool StartsWith(std::basic_string_view<T> string, std::basic_string_view<T> substring)
{
    if (string.size() < substring.size())
    {
        return false;
    }

    return boost::iequals(std::basic_string_view<T>(string.data(), substring.size()), substring);
}

template <typename T>
bool EndsWith(std::basic_string_view<T> string, std::basic_string_view<T> substring)
{
    if (string.size() < substring.size())
    {
        return false;
    }

    return boost::iequals(
        std::basic_string_view<T>(string.data() + (string.size() - substring.size()), substring.size()), substring);
}

}  // namespace

namespace Orc {

bool StartsWith(std::string_view string, std::string_view substring)
{
    return ::StartsWith(std::forward<std::string_view>(string), std::forward<std::string_view>(substring));
}

bool StartsWith(std::wstring_view string, std::wstring_view substring)
{
    return ::StartsWith(std::forward<std::wstring_view>(string), std::forward<std::wstring_view>(substring));
}

bool EndsWith(std::string_view string, std::string_view substring)
{
    return ::EndsWith(std::forward<std::string_view>(string), std::forward<std::string_view>(substring));
}

bool EndsWith(std::wstring_view string, std::wstring_view substring)
{
    return ::EndsWith(std::forward<std::wstring_view>(string), std::forward<std::wstring_view>(substring));
}

}  // namespace Orc
