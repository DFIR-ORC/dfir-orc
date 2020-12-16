//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Output/Text/Fmt/Fwd/FILE_NAME.h"

#include "NtfsDataStructures.h"

template <typename FormatContext>
auto fmt::formatter<::FILE_NAME>::format(const ::FILE_NAME& fileName, FormatContext& ctx) -> decltype(ctx.out())
{
    std::error_code ec;

    const auto name = Orc::Utf16ToUtf8(std::wstring_view(fileName.FileName, fileName.FileNameLength), ec);
    if (ec)
    {
        return formatter<std::string_view>::format(Orc::kFailedConversion, ctx);
    }

    return formatter<std::string_view>::format(name, ctx);
}

template <typename FormatContext>
auto fmt::formatter<FILE_NAME, wchar_t>::format(const FILE_NAME& fileName, FormatContext& ctx) -> decltype(ctx.out())
{
    const auto name = std::wstring_view(fileName.FileName, fileName.FileNameLength);
    return formatter<std::wstring_view, wchar_t>::format(name, ctx);
}
