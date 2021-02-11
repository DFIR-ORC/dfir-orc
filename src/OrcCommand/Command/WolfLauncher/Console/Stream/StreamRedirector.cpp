//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "StreamRedirector.h"

#include "Command/WolfLauncher/Console/Stream/WideStreambufConverter.h"
#include "Command/WolfLauncher/Console/Stream/Tee.h"

namespace Orc {
namespace Command {

StreamRedirector::StreamRedirector(std::ostream& output)
    : m_output(output)
{
}

StreamRedirector::~StreamRedirector()
{
    Reset();
}

void StreamRedirector::Take(std::ostream& input)
{
    const auto original = input.rdbuf();

    input.rdbuf(m_output.rdbuf());
    m_resetters.push_back([original, &input]() { input.rdbuf(original); });
}

void StreamRedirector::Tee(std::ostream& input)
{
    const auto original = input.rdbuf();

    auto tee = std::make_shared<Orc::Command::Tee<char>>(m_output.rdbuf(), original);
    input.rdbuf(tee.get());

    m_resetters.push_back([original, tee, &input]() { input.rdbuf(original); });
}

void StreamRedirector::Take(std::wostream& input)
{
    const auto original = input.rdbuf();

    auto wideOutput = std::make_shared<WideStreambufConverter>(m_output.rdbuf());
    input.rdbuf(wideOutput.get());

    m_resetters.push_back([original, wideOutput, &input]() { input.rdbuf(original); });
}

void StreamRedirector::Tee(std::wostream& input)
{
    const auto original = input.rdbuf();

    auto wideOutput = std::make_shared<WideStreambufConverter>(m_output.rdbuf());
    auto tee = std::make_shared<Orc::Command::Tee<wchar_t>>(wideOutput.get(), original);
    input.rdbuf(tee.get());

    m_resetters.push_back([original, tee, wideOutput, &input]() { input.rdbuf(original); });
}

void StreamRedirector::Reset()
{
    for (auto& resetter : m_resetters)
    {
        resetter();
    }

    m_resetters.clear();
}

}  // namespace Command
}  // namespace Orc
