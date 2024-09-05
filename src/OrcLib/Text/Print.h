//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string_view>
#include <functional>
#include <memory>
#include <optional>

#include "Text/Tree.h"

namespace Orc {
namespace Text {

constexpr auto kEmpty = std::string_view("<Empty>");
constexpr auto kEmptyW = std::wstring_view(L"<Empty>");
constexpr auto kError = std::string_view("<Error>");
constexpr auto kErrorW = std::wstring_view(L"<Error>");
constexpr auto kNoneAvailable = std::string_view("N/A");
constexpr auto kNoneAvailableW = std::wstring_view(L"N/A");
constexpr auto kNone = std::string_view("None");
constexpr auto kNoneW = std::wstring_view(L"None");

template <typename T>
inline void Print(Tree& node, const T& value)
{
    node.Add(L"{}", value);
}

inline std::wstring FormatKey(const std::wstring& key)
{
    std::wstring out(key);
    out.push_back(L':');
    return fmt::format(L"{:<34}", out);
}

inline void PrintKey(Tree& node, const std::wstring& key)
{
    node.AddWithoutEOL(FormatKey(key));
}

template <typename T>
inline void PrintValue(Tree& node, const std::wstring& key, const T& value)
{
    PrintKey(node, key);
    Print(node, value);
}

template <typename T>
inline void Print(Tree& node, const std::shared_ptr<T>& value)
{
    if (value == nullptr)
    {
        Print(node, kErrorW);
        return;
    }

    Print(node, *value);
}

template <typename T>
void Print(Tree& node, const std::optional<T>& value)
{
    if (!value.has_value())
    {
        Print(node, kNoneAvailableW);
        return;
    }

    Print(node, *value);
}

template <typename T>
void Print(Tree& node, const std::reference_wrapper<T> value)
{
    Print(node, value.get());
}

template <typename N, typename V>
void PrintValues(Tree& node, const N& name, const V& values)
{
    if (values.size() == 0)
    {
        PrintValue(node, name, kNoneW);
        return;
    }

    // TODO: use FormatKey
    auto valuesNode = node.AddNode(34, L"{}:", name);
    for (const auto& value : values)
    {
        Print(valuesNode, value);
    }
}

}  // namespace Text
}  // namespace Orc
