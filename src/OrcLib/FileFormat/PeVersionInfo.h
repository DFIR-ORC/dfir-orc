//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <map>
#include <optional>

#include "PeParser.h"

namespace Orc {

class PeVersionInfo
{
public:
    PeVersionInfo(ByteStream& stream, std::error_code& ec);
    PeVersionInfo(ByteStream& stream, const PeParser::PeChunk& versionInfo, std::error_code& ec);
    PeVersionInfo(BufferView versionInfo, std::error_code& ec);

    const std::optional<VS_FIXEDFILEINFO>& FixedFileInfo() const;
    const std::map<std::wstring, std::wstring>& StringFileInfo() const;

private:
    std::optional<VS_FIXEDFILEINFO> m_fixedFileInfo;
    std::map<std::wstring, std::wstring> m_stringFileInfo;
};

}  // namespace Orc
