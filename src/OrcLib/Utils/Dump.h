//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>

#include "Utils/Result.h"
#include "Utils/Guard.h"
#include "Utils/WinApi.h"

namespace Orc {

template <typename T>
Result<void> Dump(const T& container, const std::filesystem::path& output)
{
    using value_type = T::value_type;
    std::string_view buffer(reinterpret_cast<const char*>(container.data()), container.size() * sizeof(T::value_type));

    auto handle = CreateFileApi(
        output.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (!handle)
    {
        Log::Debug("Failed CreateFile [{}]", handle.error());
        return handle.error();
    }

    DWORD written = 0;
    if (!WriteFile(handle->value(), buffer.data(), static_cast<DWORD>(buffer.size()), &written, NULL))
    {
        auto ec = LastWin32Error();
        Log::Debug("Failed WriteFile [{}]", ec);
        return ec;
    }

    if (written != buffer.size())
    {
        Log::Debug("Partial write (expected: {}, done: {})", buffer.size(), written);
        return std::make_error_code(std::errc::message_size);
    }

    return Orc::Success<void>();
}

}  // namespace Orc
