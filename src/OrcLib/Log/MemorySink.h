#pragma once

//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include <spdlog/sinks/base_sink.h>

#include <memory>
#include <cassert>

namespace Orc {

template <typename T, typename Mutex>
class MemorySink : public spdlog::sinks::base_sink<Mutex>
{
public:
    MemorySink(size_t size) { m_buffer.reserve(size); }

    T& buffer() { return m_buffer; }
    const T& buffer() const { return m_buffer; }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        std::copy(
            std::cbegin(formatted),
            std::cbegin(formatted) + std::min(m_buffer.capacity() - m_buffer.size(), formatted.size()),
            std::back_inserter(m_buffer));
    }

    void flush_() override {}

private:
    T m_buffer;
};

}  // namespace Orc
