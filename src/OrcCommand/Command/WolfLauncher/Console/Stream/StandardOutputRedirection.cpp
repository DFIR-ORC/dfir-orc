//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "StandardOutputRedirection.h"

namespace Orc {
namespace Command {

StandardOutputRedirection::StandardOutputRedirection()
    : LazyFileStream<CharT>(16384)
    , m_redirector(*this)
{
}

StandardOutputRedirection ::~StandardOutputRedirection()
{
    Disable();
}

void StandardOutputRedirection::Enable()
{
    m_redirector.Tee(std::wcout);
    m_redirector.Tee(std::cout);
    m_redirector.Tee(std::wcerr);
    m_redirector.Tee(std::cerr);
}

void StandardOutputRedirection::Disable()
{
    m_redirector.Reset();
}

const std::optional<std::filesystem::path>& StandardOutputRedirection::OutputPath() const
{
    return Path();
}

}  // namespace Command
}  // namespace Orc
