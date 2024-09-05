//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "ShadowCopyStream.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

ShadowCopyStream::ShadowCopyStream(StreamReader::Ptr stream, const GUID& shadowCopyId, std::error_code& ec)
    : m_pos(0)
    , m_stream(std::move(stream))
{
    ShadowCopy::Parse(*m_stream, shadowCopyId, m_shadowCopy, ec);
    if (ec)
    {
        return;
    }
}

size_t ShadowCopyStream::Read(gsl::span<uint8_t> output, std::error_code& ec)
{
    const auto processed = m_shadowCopy.ReadAt(*m_stream, m_pos, output, ec);
    if (ec)
    {
        Log::Debug("Failed to read shadow copy (offset: {}, length: {}) [{}]", m_pos, output.size(), ec);
        return 0;
    }

    m_pos += processed;
    return processed;
}

uint64_t ShadowCopyStream::Seek(SeekDirection direction, int64_t value, std::error_code& ec)
{
    // TODO: check max with shadowcopy data
    constexpr auto kMaxOffset = std::numeric_limits<int64_t>::max();

    switch (direction)
    {
        case SeekDirection::kBegin:
            if (value < 0 || value > kMaxOffset)
            {
                ec = std::make_error_code(std::errc::invalid_seek);
                return -1;
            }

            m_pos = value;
            break;
        case SeekDirection::kCurrent:
            if (m_pos - value < 0 || m_pos + value > kMaxOffset)
            {
                ec = std::make_error_code(std::errc::invalid_seek);
                return -1;
            }

            m_pos = m_pos + value;
            break;
        case SeekDirection::kEnd:
            ec = std::make_error_code(std::errc::invalid_seek);
            return 0;
    }

    return m_pos;
}

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
