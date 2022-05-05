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

#include "Stream/SeekDirection.h"
#include "Utils/BufferView.h"
#include "Utils/MetaPtr.h"

namespace Orc {

template <typename ContainerT>
class BufferStreamConcept
{
public:
    using value_type = ContainerT;

    BufferStreamConcept()
        : m_buffer()
        , m_pos(0)
    {
    }

    BufferStreamConcept(ContainerT buffer)
        : m_buffer(std::move(buffer))
        , m_pos(0)
    {
    }

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec)
    {
        const auto start = m_pos;
        const auto end = std::min(m_buffer->size() - m_pos, output.size());

        auto newEnd =
            std::copy(std::cbegin(*m_buffer) + start, std::cbegin(*m_buffer) + start + end, std::begin(output));

        uint64_t processed = std::distance(std::cbegin(output), newEnd);
        m_pos += processed;
        return processed;
    }

    size_t Write(BufferView input, std::error_code& ec)
    {
        const auto initialSize = m_buffer->size();

        std::copy(std::cbegin(input), std::cend(input), std::back_inserter(*m_buffer));

        auto processed = m_buffer->size() - initialSize;
        m_pos += processed;
        return processed;
    }

    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec)
    {
        if (direction == SeekDirection::kBegin)
        {
            if (value < 0 || value >= m_buffer->size())
            {
                ec = std::make_error_code(std::errc::invalid_seek);
                return -1;
            }

            m_pos = value;
            return m_pos;
        }
        else if (direction == SeekDirection::kCurrent)
        {
            const auto pos = m_pos + value;

            if (pos < 0 || pos >= m_buffer->size())
            {
                ec = std::make_error_code(std::errc::invalid_seek);
                return -1;
            }

            m_pos = pos;
            return m_pos;
        }
        else if (direction == SeekDirection::kEnd)
        {
            const auto pos = m_buffer->size() - value;

            if (pos < 0 || pos >= m_buffer->size())
            {
                ec = std::make_error_code(std::errc::invalid_seek);
                return -1;
            }

            m_pos = value;
            return m_pos;
        }
        else
        {
            ec = std::make_error_code(std::errc::invalid_seek);
            return -1;
        }
    }

    const ContainerT& Buffer() const { return *m_buffer; }
    ContainerT& Buffer() { return *m_buffer; }

private:
    MetaPtr<ContainerT> m_buffer;
    uint64_t m_pos;
};

}  // namespace Orc
