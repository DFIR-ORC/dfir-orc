//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <array>
#include <string_view>

#include "Text/Format.h"

namespace Orc {
namespace Text {

constexpr auto kNodeHeader = std::string_view("[+] ");
constexpr auto kNodeHeaderW = std::wstring_view(L"[+] ");

constexpr auto kIndentLevel0 = std::string_view("");
constexpr auto kIndentLevel1 = std::string_view("    ");
constexpr auto kIndentLevel2 = std::string_view("        ");
constexpr auto kIndentLevel3 = std::string_view("            ");
constexpr auto kIndentLevel4 = std::string_view("                ");
constexpr auto kIndentLevel5 = std::string_view("                    ");
constexpr auto kIndentLevel6 = std::string_view("                        ");
constexpr auto kIndentLevel7 = std::string_view("                            ");
constexpr auto kIndentLevel8 = std::string_view("                                ");
constexpr auto kIndentLevel9 = std::string_view("                                    ");

constexpr std::array kIndentLevels = {
    kIndentLevel0,
    kIndentLevel1,
    kIndentLevel2,
    kIndentLevel3,
    kIndentLevel4,
    kIndentLevel5,
    kIndentLevel6,
    kIndentLevel7,
    kIndentLevel8,
    kIndentLevel9};

constexpr auto kIndentLevel0W = std::wstring_view(L"");
constexpr auto kIndentLevel1W = std::wstring_view(L"    ");
constexpr auto kIndentLevel2W = std::wstring_view(L"        ");
constexpr auto kIndentLevel3W = std::wstring_view(L"            ");
constexpr auto kIndentLevel4W = std::wstring_view(L"                ");
constexpr auto kIndentLevel5W = std::wstring_view(L"                    ");
constexpr auto kIndentLevel6W = std::wstring_view(L"                        ");
constexpr auto kIndentLevel7W = std::wstring_view(L"                            ");
constexpr auto kIndentLevel8W = std::wstring_view(L"                                ");
constexpr auto kIndentLevel9W = std::wstring_view(L"                                    ");

constexpr std::array kIndentLevelsW = {
    kIndentLevel0W,
    kIndentLevel1W,
    kIndentLevel2W,
    kIndentLevel3W,
    kIndentLevel4W,
    kIndentLevel5W,
    kIndentLevel6W,
    kIndentLevel7W,
    kIndentLevel8W,
    kIndentLevel9W};

template <typename T>
const std::basic_string_view<T>& GetIndent(uint16_t level);

template <typename B>
class Tree
{
public:
    using value_type = typename B::value_type;

private:
    static std::basic_string<value_type> CreateHeader(uint16_t offset, uint16_t indentLevelIndex)
    {
        std::basic_string<value_type> header(offset, Traits::space_v<value_type>);
        const auto& indentation = GetIndent<value_type>(indentLevelIndex);
        std::copy(std::cbegin(indentation), std::cend(indentation), std::back_inserter(header));
        return header;
    }

public:
    /*!
     * \brief Create a named node
     *
     * Helper to create a structured text tree output.
     *
     * The text output is built on the following:
     *   [offset][indentation][custom_text]
     *
     *   offset: number of spaces from the line start
     *   indentLevelIndex: indentation level
     *   FmtArgs: forward parameters to fmt to construct root's name
     */
    template <typename... FmtArgs>
    Tree(B& buffer, uint16_t offset, uint16_t indentLevelIndex, FmtArgs&&... args)
        : m_offset(offset)
        , m_indentLevel(indentLevelIndex + 1)
        , m_header(CreateHeader(offset, indentLevelIndex + 1))
        , m_buffer(buffer)
        , m_indentationIsDone(false)
    {
        FormatTo(std::back_inserter(m_buffer), CreateHeader(offset, indentLevelIndex));
        FormatTo(std::back_inserter(m_buffer), std::forward<FmtArgs>(args)...);
    }

    /*!
     * \brief Create an unnamed node
     *
     *   offset: number of spaces from the line start
     *   indentLevelIndex: initial indentation level
     */
    Tree(B& buffer, uint16_t offset, uint16_t indentLevelIndex)
        : m_offset(offset)
        , m_indentLevel(indentLevelIndex + 1)
        , m_header(CreateHeader(offset, indentLevelIndex))
        , m_buffer(buffer)
        , m_indentationIsDone(false)
    {
    }

    /*!
     * \brief Add a node with an added offset to the current indentation
     *
     *   T: offset added to the current indentation
     *   FmtArgs: forward parameters to fmt to construct root's name
     */
    template <typename T, typename... FmtArgs>
    std::enable_if_t<std::is_integral_v<T>, Tree> AddNode(T offset, FmtArgs&&... args)
    {
        Add(std::forward<FmtArgs>(args)...);
        return Tree(m_buffer, m_offset + offset, m_indentLevel - 1);
    }

    template <typename... FmtArgs>
    Tree AddNode(FmtArgs&&... args)
    {
        Add(std::forward<FmtArgs>(args)...);
        return Tree(m_buffer, m_offset, m_indentLevel);
    }

    template <typename... FmtArgs>
    void Add(FmtArgs&&... args)
    {
        if (!m_indentationIsDone)
        {
            FormatToWithoutEOL(std::back_inserter(m_buffer), m_header);
        }

        FormatTo(std::back_inserter(m_buffer), std::forward<FmtArgs>(args)...);
        m_indentationIsDone = false;
    }

    template <typename... FmtArgs>
    void AddWithoutEOL(FmtArgs&&... args)
    {
        if (!m_indentationIsDone)
        {
            FormatToWithoutEOL(std::back_inserter(m_buffer), m_header);
            m_indentationIsDone = true;
        }

        FormatToWithoutEOL(std::back_inserter(m_buffer), std::forward<FmtArgs>(args)...);
    }

    template <typename... FmtArgs>
    void Append(FmtArgs&&... args)
    {
        FormatToWithoutEOL(std::back_inserter(m_buffer), std::forward<FmtArgs>(args)...);
    }

    template <typename T>
    void AddHexDump(T&& description, const std::string_view& data)
    {
        m_indentationIsDone = false;

        // TODO: Is it needed to handle custom header ?
        Add(std::forward<T>(description));
        HexDump(
            GetIndent<value_type>(m_indentLevel + 1), std::cbegin(data), std::cend(data), std::back_inserter(m_buffer));
    }

    void AddEOL()
    {
        m_indentationIsDone = false;
        m_buffer.push_back(Traits::newline_v<value_type>);
    }

    void AddEmptyLine() { AddEOL(); }

private:
    const uint16_t m_offset;
    const uint16_t m_indentLevel;
    const std::basic_string<value_type> m_header;
    B& m_buffer;
    bool m_indentationIsDone;
};

}  // namespace Text
}  // namespace Orc
