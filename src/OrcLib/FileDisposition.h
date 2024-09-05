//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <string_view>
#include <system_error>
#include <ios>

namespace Orc {

enum class FileDisposition
{
    Unknown,
    Append,
    Truncate,
    CreateNew
};

std::wstring_view ToString(FileDisposition disposition);
Orc::Result<FileDisposition> ToFileDisposition(const std::wstring& disposition);

std::ios_base::openmode ToOpenMode(FileDisposition disposition);

}  // namespace Orc
