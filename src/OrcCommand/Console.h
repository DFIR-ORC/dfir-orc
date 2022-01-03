//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

// Note: Console use an adapter over stdout because current FormatTo implementation requires a container, FormatTo could
// be changed to require OutputIterator (this could prevent to do inplace utf conversion) ?

#pragma once

#include <fmt/format.h>

#include <iostream>

#include "Text/Format.h"
#include "Text/Tree.h"
#include "Utils/TypeTraits.h"

namespace Orc {
namespace Command {

class Console
{
public:
    // To avoid any unrequired encoding conversion string arguments should have identical 'value_type'
    using value_type = wchar_t;

    using Buffer = StdoutContainerAdapter<value_type>;

    Console()
        : m_stdout({})
        , m_tree(Text::Tree(m_stdout, 0, 0))
    {
    }

    // Print to stdout with the given fmt parameters followed by 'newline' character
    template <typename... FmtArgs>
    void Print(FmtArgs&&... args)
    {
        Text::FormatTo(std::back_inserter(m_stdout), std::forward<FmtArgs>(args)...);
    }

    // Print to stdout with the given fmt parameters followed by 'newline' character
    template <typename... FmtArgs>
    void Print(int indentationLevel, FmtArgs&&... args)
    {
        const auto& indentation = Text::GetIndent<value_type>(indentationLevel);
        std::copy(std::cbegin(indentation), std::cend(indentation), std::back_inserter(m_stdout));
        Text::FormatTo(std::back_inserter(m_stdout), std::forward<FmtArgs>(args)...);
    }

    // Print to stdout the 'newline' character
    void PrintNewLine() { m_stdout.push_back(Traits::newline_v<value_type>); }

    const Orc::Text::Tree& OutputTree() const { return m_tree; }

    Orc::Text::Tree& OutputTree() { return m_tree; }

    template <typename... Args>
    Orc::Text::Tree CreateOutputTree(uint16_t offset, uint16_t indentationLevel, Args&&... args)
    {
        return Text::Tree(m_stdout, offset, indentationLevel, std::forward<Args>(args)...);
    }

    // Write into the console's buffer the given fmt parameters without 'newline' character
    template <typename... FmtArgs>
    void Write(FmtArgs&&... args)
    {
        Text::FormatToWithoutEOL(std::back_inserter(m_stdout), std::forward<FmtArgs>(args)...);
    }

    // Write into the console's buffer the given fmt parameters without 'newline' character
    template <typename... FmtArgs>
    void Write(int indentationLevel, FmtArgs&&... args)
    {
        const auto& indentation = Text::GetIndent<value_type>(indentationLevel);
        std::copy(std::cbegin(indentation), std::cend(indentation), std::back_inserter(m_stdout));
        Text::FormatToWithoutEOL(std::back_inserter(m_stdout), std::forward<FmtArgs>(args)...);
    }

    // Append character to the console buffer
    void push_back(value_type c) { m_stdout.push_back(c); }

    void Flush() { m_stdout.flush(); }

private:
    Buffer m_stdout;
    Text::Tree m_tree;
};

}  // namespace Command
}  // namespace Orc
