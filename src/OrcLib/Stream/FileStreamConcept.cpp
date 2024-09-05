//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "FileStreamConcept.h"

#include <cstdint>
#include <system_error>
#include <vector>

#include "Log/Log.h"

#include "Text/Fmt/std_error_code.h"

using namespace Orc::Guard;

namespace Orc {

FileStreamConcept::FileStreamConcept(FileHandle&& handle)
    : m_handle(std::move(handle))
{
}

size_t FileStreamConcept::Read(gsl::span<uint8_t> output, std::error_code& ec)
{
    DWORD dwNumberOfBytesRead = 0;

    const auto ret = ::ReadFile(m_handle.value(), output.data(), output.size(), &dwNumberOfBytesRead, NULL);
    if (!ret)
    {
        ec.assign(::GetLastError(), std::system_category());
        Log::Debug("Failed to read file [{}]", ec);
        return dwNumberOfBytesRead;
    }

    return dwNumberOfBytesRead;
}

size_t FileStreamConcept::Write(BufferView input, std::error_code& ec)
{
    size_t totalWritten = 0;
    DWORD written = 0;

    for (; totalWritten < input.size(); totalWritten += written)
    {
        auto ret =
            ::WriteFile(m_handle.value(), input.data() + totalWritten, input.size() - totalWritten, &written, NULL);
        if (!ret)
        {
            ec.assign(::GetLastError(), std::system_category());
            Log::Debug("Failed to write file [{}]", ec);
            return written;
        }

        totalWritten += written;
    }

    return totalWritten;
}

uint64_t FileStreamConcept::Seek(SeekDirection direction, int64_t value, std::error_code& ec)
{
    LARGE_INTEGER newOffset = {0};

    auto ret =
        ::SetFilePointerEx(m_handle.value(), *reinterpret_cast<LARGE_INTEGER*>(&value), &newOffset, ToWin32(direction));
    if (!ret)
    {
        ec.assign(::GetLastError(), std::system_category());
        Log::Debug("Failed to seek file [{}]", ec);
        return newOffset.QuadPart;
    }

    return newOffset.QuadPart;
}

}  // namespace Orc
