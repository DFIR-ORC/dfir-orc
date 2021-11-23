//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "StandardOutputFileTee.h"

#include "Utils/StdStream/WideStreambufConverter.h"

namespace Orc {
namespace Command {

StandardOutputFileTee::StandardOutputFileTee()
    : LazyFileStream<CharT>(16384)
    , m_wideConversionStreambuf(std::make_unique<WideStreambufConverter>(rdbuf()))
    , m_wideConversionStream(m_wideConversionStreambuf.get())
    , m_wideRedirection(m_wideConversionStream)
    , m_redirection(*this)
{
}

void StandardOutputFileTee::Enable()
{
    m_wideRedirection.Tee(std::wcout);
    m_wideRedirection.Tee(std::wcerr);
    m_redirection.Tee(std::cout);
    m_redirection.Tee(std::cerr);
}

void StandardOutputFileTee::Disable()
{
    m_redirection.Reset();
    m_wideRedirection.Reset();
}

const std::optional<std::filesystem::path>& StandardOutputFileTee::OutputPath() const
{
    return Path();
}

}  // namespace Command
}  // namespace Orc
