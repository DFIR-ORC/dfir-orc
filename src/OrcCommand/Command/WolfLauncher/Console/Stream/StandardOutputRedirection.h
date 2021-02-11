//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <memory>

#include "Command/WolfLauncher/Console/Stream/LazyFileStream.h"

#include "Command/WolfLauncher/Console/Stream/StreamRedirector.h"

namespace Orc {
namespace Command {

class StandardOutputRedirection : public LazyFileStream<char>
{
public:
    using Ptr = std::shared_ptr<StandardOutputRedirection>;
    using CharT = LazyFileStream<char>::char_type;

    StandardOutputRedirection();
    ~StandardOutputRedirection() override;

    void Enable();
    void Disable();

    const std::optional<std::filesystem::path>& OutputPath() const;

private:
    StreamRedirector m_redirector;
};

}  // namespace Command
}  // namespace Orc
