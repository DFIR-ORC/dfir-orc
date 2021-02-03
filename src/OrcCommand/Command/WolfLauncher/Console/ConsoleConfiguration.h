//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <optional>

#include "FileDisposition.h"
#include "Text/Encoding.h"

namespace Orc {

class ConfigItem;

namespace Command {

class UtilitiesLogger;
class StandardOutputRedirection;

struct ConsoleConfiguration
{
    static void Parse(int argc, const wchar_t* argv[], ConsoleConfiguration& configuration);

    static HRESULT Register(ConfigItem& parent, DWORD dwIndex);
    static void Parse(const ConfigItem& item, ConsoleConfiguration& configuration);

    static void Apply(StandardOutputRedirection& redirection, const ConsoleConfiguration& config);

    struct OutputFile
    {
        std::optional<std::wstring> path;
        std::optional<FileDisposition> disposition;
        std::optional<Text::Encoding> encoding;
    };

    OutputFile output;
};

}  // namespace Command
}  // namespace Orc
