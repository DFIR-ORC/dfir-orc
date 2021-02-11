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

namespace Orc {
namespace Command {

class StreamRedirector final
{
public:
    explicit StreamRedirector(std::ostream& output);
    ~StreamRedirector();

    void Take(std::ostream& input);
    void Take(std::wostream& input);

    void Tee(std::ostream& input);
    void Tee(std::wostream& input);

    void Reset();

private:
    std::ostream& m_output;
    std::vector<std::function<void()>> m_resetters;
};

}  // namespace Command
}  // namespace Orc
