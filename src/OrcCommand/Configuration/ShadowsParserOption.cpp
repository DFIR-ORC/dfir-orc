//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ShadowsParserOption.h"

namespace Orc {

void ParseShadowsParserOption(
    std::wstring_view shadowsParser,
    std::optional<Ntfs::ShadowCopy::ParserType>& parser,
    std::error_code& ec)
{
    if (shadowsParser.empty())
    {
        return;
    }

    auto value = Ntfs::ShadowCopy::ToParserType(shadowsParser, ec);
    if (ec)
    {
        return;
    }

    parser = value;
}

}  // namespace Orc
