//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>
#include <system_error>

#include <gsl/span>

#include "Stream/SeekDirection.h"
#include "Utils/BufferView.h"
#include "Utils/MetaPtr.h"

namespace Orc {

template <typename StreamT>
class SpanStreamConcept
{
public:
    SpanStreamConcept(StreamT stream, uint64_t startOffset, uint64_t size)
        : m_stream(std::move(stream))
        , m_startOffset(startOffset)
        , m_size(size)
        , m_offset(0)
    {
    }

    SpanStreamConcept(SpanStreamConcept&&) = default;

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec)
    {
        uint64_t processed = 0;

        const auto remaining = m_size - (m_offset - m_startOffset);
        if (output.size() > remaining)
        {
            auto sub = gsl::span<uint8_t>(output.data(), remaining);
            processed = m_stream->Read(sub, ec);
        }
        else
        {
            processed = m_stream->Read(output, ec);
        }

        m_offset += processed;
        return processed;
    }

    size_t Write(BufferView input, std::error_code& ec)
    {
        uint64_t processed = 0;
        const auto remaining = m_startOffset + m_offset - m_size;

        if (input.size() > remaining)
        {
            BufferView inputSlice(input.data(), remaining);
            processed = m_stream->Write(inputSlice, ec);
            m_offset += processed;
            ec = std::make_error_code(std::errc::no_buffer_space);
        }
        else
        {
            processed = m_stream->Write(input, ec);
            m_offset += processed;
        }

        return processed;
    }

    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec)
    {
        uint64_t newOffset = 0;

        switch (direction)
        {
            case SeekDirection::kBegin:
                if (value < 0 || value > m_size)
                {
                    ec = std::make_error_code(std::errc::invalid_seek);
                    return -1;
                }

                newOffset = m_startOffset + value;
                break;
            case SeekDirection::kCurrent:
                if (m_offset - value < m_startOffset || m_offset + value > m_size)
                {
                    ec = std::make_error_code(std::errc::invalid_seek);
                    return -1;
                }

                newOffset = m_offset + value;
                break;
            case SeekDirection::kEnd:
                if (value > 0 || m_size + value < 0)
                {
                    ec = std::make_error_code(std::errc::invalid_seek);
                    return -1;
                }

                newOffset = m_size - 1 + value;
                break;
        }

        m_stream->Seek(direction, newOffset, ec);
        if (ec)
        {
            return -1;
        }

        m_offset = newOffset;
        return m_offset - m_startOffset;
    }

    StreamT& UnderlyingStream() { return m_stream; }
    const StreamT& UnderlyingStream() const { return m_stream; }

private:
    MetaPtr<StreamT> m_stream;
    uint64_t m_startOffset;
    uint64_t m_size;
    uint64_t m_offset;
};

}  // namespace Orc
