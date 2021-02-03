//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <mutex>
#include <ostream>

namespace Orc {
namespace Log {

template <typename Mutex>
class wostream_sink final : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit wostream_sink(std::wostream& stream, bool alwaysFlush)
        : m_output(stream)
        , m_alwaysFlush(alwaysFlush)
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        m_output.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
        if (m_alwaysFlush)
        {
            m_output.flush();
        }
    }

    void flush_() override { m_output.flush(); }

private:
    std::wostream& m_output;
    bool m_alwaysFlush;
};

using wostream_sink_mt = wostream_sink<std::mutex>;
using wostream_sink_st = wostream_sink<spdlog::details::null_mutex>;

}  // namespace Log
}  // namespace Orc
