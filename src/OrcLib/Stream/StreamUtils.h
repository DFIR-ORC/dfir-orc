//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include <gsl/span>

#include "Log/Log.h"
#include "Utils/BufferView.h"
#include "Utils/TypeTraits.h"
#include "Text/Fmt/ByteQuantity.h"
#include "Text/Fmt/Offset.h"
#include "Stream/SeekDirection.h"

namespace Orc {
namespace Stream {

/*!
 * \brief Read 'stream' and shrink output to read size.
 */
template <typename InputStreamT, typename CharT>
size_t Read(InputStreamT& stream, gsl::span<CharT> output, std::error_code& ec)
{
    gsl::span<uint8_t> span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(CharT));
    return stream.Read(span, ec);
}

/*!
 * \brief Read 'stream' until 'output' is completely filled.
 */
template <typename InputStreamT, typename CharT>
size_t ReadChunk(InputStreamT& stream, gsl::span<CharT> output, std::error_code& ec)
{
    uint64_t processed = 0;

    while (processed != output.size())
    {
        // TODO: subview
        gsl::span<uint8_t> span(
            reinterpret_cast<uint8_t*>(output.data() + processed), output.size() * sizeof(CharT) - processed);

        const auto lastReadSize = stream.Read(span, ec);
        if (ec)
        {
            return processed;
        }

        if (lastReadSize == 0)
        {
            Log::Debug("Input stream does not provide enough bytes to fill buffer");
            ec = std::make_error_code(std::errc::message_size);
            return processed;
        }

        processed += lastReadSize;
    }

    return processed;
}

/*!
 * \brief Read 'stream' until 'output' is completely filled.
 */
template <typename InputStreamT, typename ContainerT>
size_t Read(InputStreamT& stream, ContainerT& output, std::error_code& ec)
{
    gsl::span<uint8_t> span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(ContainerT::value_type));

    size_t processed = stream.Read(span, ec);
    if (ec)
    {
        return processed;
    }

    output.resize(processed / sizeof(ContainerT::value_type));
    return processed;
}

/*!
 * \brief Read 'stream' until 'output' is completely filled.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename InputStreamT, typename ContainerT>
size_t ReadChunk(InputStreamT& stream, ContainerT& output, std::error_code& ec)
{
    gsl::span<uint8_t> span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(ContainerT::value_type));
    return ReadChunk(stream, span, ec);
}

/*!
 * \brief Read at specified position the available bytes and store them in container of type ContainerT
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename InputStreamT, typename ContainerT>
size_t ReadAt(InputStreamT& stream, uint64_t offset, ContainerT& output, std::error_code& ec)
{
    stream.Seek(SeekDirection::kBegin, offset, ec);
    if (ec)
    {
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    return Read(stream, output, ec);
}

/*!
 * \brief Read at specified position in 'stream' until 'output' is completely filled.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename InputStreamT, typename ContainerT>
size_t ReadChunkAt(InputStreamT& stream, uint64_t offset, ContainerT& output, std::error_code& ec)
{
    stream.Seek(SeekDirection::kBegin, offset, ec);
    if (ec)
    {
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    gsl::span<uint8_t> span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(ContainerT::value_type));
    return ReadChunk(stream, span, ec);
}

/*!
 * \brief Read at specified position in 'stream' until 'output' is completely filled.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename InputStreamT, typename CharT>
size_t
ReadChunkAt(InputStreamT& stream, uint64_t offset, size_t chunkSizeCb, gsl::span<CharT> output, std::error_code& ec)
{
    stream.Seek(SeekDirection::kBegin, offset, ec);
    if (ec)
    {
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    assert(chunkSizeCb < output.size() * sizeof(CharT) && "Buffer 'output' overflow");

    gsl::span<uint8_t> span(reinterpret_cast<uint8_t*>(output.data()), chunkSizeCb);
    return ReadChunk(stream, span, ec);
}

/*!
 * \brief Fill an item of type 'T' and fail if there is not enough data to read.
 */
template <typename InputStreamT, typename ItemT>
void ReadItem(InputStreamT& stream, ItemT& output, std::error_code& ec)
{
    uint64_t processed = stream.Read(gsl::span<uint8_t>(reinterpret_cast<uint8_t*>(&output), sizeof(output)), ec);
    if (ec)
    {
        return;
    }

    if (processed != sizeof(output))
    {
        ec = std::make_error_code(std::errc::interrupted);
        Log::Debug("Failed to read expected size ({}/{})", processed, sizeof(ItemT));
        return;
    }
}

/*!
 * \brief Fill an item of type 'T' and fail if there is not enough data to read.
 */
template <typename InputStreamT, typename ItemT>
void ReadItemAt(InputStreamT& stream, uint64_t offset, ItemT& output, std::error_code& ec)
{
    stream.Seek(SeekDirection::kBegin, offset, ec);
    if (ec)
    {
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return;
    }

    ReadItem(stream, output, ec);
}

}  // namespace Stream
}  // namespace Orc
