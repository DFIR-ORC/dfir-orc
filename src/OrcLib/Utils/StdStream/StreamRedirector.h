//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <functional>
#include <ostream>
#include <vector>

#include "Utils/StdStream/Tee.h"

namespace Orc {

template <typename CharT>
class StreamRedirector final
{
public:
    explicit StreamRedirector(std::basic_ostream<CharT>& output)
        : m_output(output)
    {
    }

    ~StreamRedirector() { Reset(); }

    void Take(std::basic_ostream<CharT>& input)
    {
        const auto original = input.rdbuf();
        input.rdbuf(m_output.rdbuf());
        m_resetters.push_back([original, &input]() { input.rdbuf(original); });
    }

    void Tee(std::basic_ostream<CharT>& input)
    {
        const auto original = input.rdbuf();
        auto tee = std::make_shared<Orc::Tee<CharT>>(m_output.rdbuf(), original);
        input.rdbuf(tee.get());

        m_resetters.push_back([original, tee, &input]() { input.rdbuf(original); });
    }

    void Reset()
    {
        for (auto& resetter : m_resetters)
        {
            resetter();
        }

        m_resetters.clear();
    }

private:
    std::basic_ostream<CharT>& m_output;
    std::vector<std::function<void()>> m_resetters;
};

extern template class StreamRedirector<char>;
extern template class StreamRedirector<wchar_t>;

}  // namespace Orc
