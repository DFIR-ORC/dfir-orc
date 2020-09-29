//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <functional>

#include "Output/Text/Tree.h"

namespace Orc {
namespace Text {

constexpr auto kStringEmpty = std::string_view("<Empty>");
constexpr auto kStringEmptyW = std::wstring_view(L"<Empty>");

// Default function to be specialized for custom output
template <typename T, typename N>
void PrintKey(Orc::Text::Tree<T>& root, const N& key)
{
    using value_type = Traits::underlying_char_type_t<N>;

    std::basic_string<value_type> decoratedKey(key);
    decoratedKey.push_back(Traits::semicolon_v<value_type>);
    root.AddWithoutEOL(L"{:<34}", decoratedKey);
}

// Default function to be specialized for custom output
template <typename T, typename V>
void Print(Orc::Text::Tree<T>& root, const V& value)
{
    root.Add(L"{}", value);
}

template <typename T, typename V>
void Print(Orc::Text::Tree<T>& root, const std::reference_wrapper<V> value)
{
    Print(root, value.get());
}

// Default function to be specialized for custom output
template <typename T, typename N, typename V>
void PrintValue(Orc::Text::Tree<T>& root, const N& name, const V& value)
{
    PrintKey(root, name);
    Print(root, value);
}

template <typename T, typename N, typename V>
void PrintValues(Orc::Text::Tree<T>& root, const N& name, const V& values)
{
    if (values.size() == 0)
    {
        PrintValue(root, name, L"None");
        return;
    }

    auto node = root.AddNode(34, L"{}:", name);
    for (const auto& value : values)
    {
        Print(node, value);
    }
}

}  // namespace Text
}  // namespace Orc
