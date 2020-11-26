//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

//
// WARNING: This sink requires the ByteStream to avoid calling any spdlog method
// that would provoke an endless recursion.
//

#pragma once

#include <spdlog/sinks/base_sink.h>

#include <memory>
#include <cassert>

#include "ByteStream.h"

template <typename Mutex>
class ByteStreamSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    ByteStreamSink() {}

    ByteStreamSink(std::shared_ptr<Orc::ByteStream> stream)
        : m_byteStream(std::move(stream))
    {
    }

    void SetStream(std::shared_ptr<Orc::ByteStream> stream) { m_stream = std::move(stream); }

    void CloseStream()
    {
        auto stream = std::move(m_stream);
        m_stream.reset();
        stream->Close();
    }

    const std::shared_ptr<Orc::ByteStream>& stream() { return m_stream; }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (m_stream == nullptr)
        {
            return;
        }

        spdlog::memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);
        const std::string buffer = fmt::to_string(formatted);

        ULONGLONG cbWritten = 0;
        HRESULT hr = m_stream->Write((const PVOID)(buffer.data()), buffer.size(), &cbWritten);
        _ASSERT(SUCCEEDED(hr) && cbWritten == buffer.size());
    }

    void flush_() override { return; }

private:
    std::shared_ptr<Orc::ByteStream> m_stream;
};
