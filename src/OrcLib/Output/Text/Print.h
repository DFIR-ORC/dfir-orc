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
#include <memory>
#include <optional>

#include "Output/Text/Tree.h"

namespace Orc {
namespace Text {

constexpr auto kStringEmpty = std::string_view("<Empty>");
constexpr auto kStringEmptyW = std::wstring_view(L"<Empty>");
constexpr auto kError = std::string_view("<Error>");
constexpr auto kErrorW = std::wstring_view(L"<Error>");
constexpr auto kNoneAvailable = std::string_view("N/A");
constexpr auto kNoneAvailableW = std::wstring_view(L"N/A");
constexpr auto kNone = std::string_view("None");
constexpr auto kNoneW = std::wstring_view(L"None");

template <typename T, typename N>
void PrintKey(Orc::Text::Tree<T>& root, const N& key)
{
    using value_type = Traits::underlying_char_type_t<N>;

    std::basic_string<value_type> decoratedKey(key);
    decoratedKey.push_back(Traits::semicolon_v<value_type>);
    root.AddWithoutEOL(L"{:<34}", decoratedKey);
}

// To be specialized...
template <typename V>
struct Printer
{
    template <typename T>
    static void Output(Orc::Text::Tree<T>& root, const V& value)
    {
        root.Add(L"{}", value);
    }
};

template <typename T, typename V>
void Print(Orc::Text::Tree<T>& root, const V& value)
{
    Printer<V>::Output(root, value);
}

template <typename T, typename V>
void Print(Orc::Text::Tree<T>& root, const std::shared_ptr<V>& value)
{
    if (value == nullptr)
    {
        Print(root, kErrorW);
        return;
    }

    Printer<V>::Output(root, *value);
}

template <typename T, typename V>
void Print(Orc::Text::Tree<T>& root, const std::optional<V>& item)
{
    if (!item.has_value())
    {
        Print(root, kNoneAvailableW);
        return;
    }

    Printer<V>::Print(root, *item);
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
        PrintValue(root, name, kNoneW);
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
