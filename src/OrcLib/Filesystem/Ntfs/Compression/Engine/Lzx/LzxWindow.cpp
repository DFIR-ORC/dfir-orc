//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "LzxWindow.h"

#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

namespace {

// Both the offset and the length come from the compressed stream. An offset must name a byte this chunk has
// already produced, and a length must fit what is left: either one out of range means the stream is forged.
bool IsMatchValid(size_t produced, size_t remaining, uint32_t offset, uint32_t length)
{
    if (offset == 0 || offset > produced)
    {
        return false;
    }

    if (length == 0 || length > remaining)
    {
        return false;
    }

    return true;
}

}  // namespace

namespace Orc::Ntfs::Lzx {

bool Window::WriteLiteral(uint8_t value, std::error_code& ec)
{
    if (Remaining() == 0)
    {
        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug("Lzx: a literal past the {} bytes of output [{}]", m_output.size(), ec);
        return false;
    }

    m_output[m_produced++] = value;
    return true;
}

bool Window::WriteMatch(uint32_t offset, uint32_t length, std::error_code& ec)
{
    if (!IsMatchValid(m_produced, Remaining(), offset, length))
    {
        ec = std::make_error_code(std::errc::bad_message);
        Log::Debug(
            "Lzx: match offset {} length {} invalid at {} produced of {} [{}]",
            offset,
            length,
            m_produced,
            m_output.size(),
            ec);
        return false;
    }

    // Byte by byte on purpose: an offset smaller than the length makes the match overlap itself, which is how
    // LZX codes a run, so a block copy would be wrong.
    size_t source = m_produced - offset;
    for (uint32_t i = 0; i < length; ++i)
    {
        m_output[m_produced++] = m_output[source++];
    }

    return true;
}

}  // namespace Orc::Ntfs::Lzx
