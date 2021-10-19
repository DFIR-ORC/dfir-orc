//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Stream/StreamConcept.h"
#include "Utils/AnyPtr.h"

namespace Orc {

template <typename ByteStreamT>
class ByteStreamConcept
{
public:
    ByteStreamConcept()
        : m_stream()
    {
    }

    explicit ByteStreamConcept(ByteStreamT stream)
        : m_stream(std::move(stream))
    {
    }

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec)
    {
        uint64_t processed = 0;
        HRESULT hr = m_stream->Read(output.data(), output.size(), &processed);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Debug("Failed to read from byte stream [{}]", ec);
            return processed;
        }

        return processed;
    }

    size_t Write(BufferView& input, std::error_code& ec)
    {
        uint64_t processed = 0;
        HRESULT hr = m_stream->Write((PVOID)(input.data()), input.size(), &processed);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Debug("Failed to write to byte stream [{}]", ec);
            return processed;
        }

        return processed;
    }

    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec)
    {
        uint64_t currentOffset = 0;
        HRESULT hr = m_stream->SetFilePointer(value, ToWin32(direction), &currentOffset);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Debug("Failed to seek byte stream [{}]", ec);
            return currentOffset;
        }

        return currentOffset;
    }

private:
    AnyPtr<ByteStreamT> m_stream;
};

}  // namespace Orc
