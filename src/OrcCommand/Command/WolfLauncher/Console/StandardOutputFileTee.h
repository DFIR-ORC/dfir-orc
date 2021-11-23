//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include "Utils/StdStream/LazyFileStream.h"

#include "Utils/StdStream/StreamRedirector.h"

namespace Orc {
namespace Command {

class StandardOutputFileTee : public LazyFileStream<char>
{
public:
    using Ptr = std::shared_ptr<StandardOutputFileTee>;
    using CharT = LazyFileStream<char>::char_type;

    StandardOutputFileTee();

    const std::optional<std::filesystem::path>& OutputPath() const;

    void Enable();
    void Disable();

private:
    std::unique_ptr<std::wstreambuf> m_wideConversionStreambuf;
    std::wostream m_wideConversionStream;
    StreamRedirector<wchar_t> m_wideRedirection;
    StreamRedirector<char> m_redirection;
};

}  // namespace Command
}  // namespace Orc
