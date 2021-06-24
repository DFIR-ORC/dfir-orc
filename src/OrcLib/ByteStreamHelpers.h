//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include "ByteStream.h"

#include <system_error>

#include "Log/Log.h"
#include "Text/Fmt/Offset.h"
#include "Utils/BufferView.h"
#include "Utils/BufferSpan.h"
#include "Utils/TypeTraits.h"

namespace Orc {

/*!
 * \brief Read 'stream' and shrink output to read size.
 */
template <typename CharT>
size_t Read(ByteStream& stream, BasicBufferSpan<CharT> output, std::error_code& ec)
{
    uint64_t cbBytesRead = 0;

    HRESULT hr = stream.Read(output.data(), output.size() * sizeof(CharT), &cbBytesRead);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to read stream [{}]", ec);
        return cbBytesRead;
    }

    return cbBytesRead;
}

/*!
 * \brief Read 'stream' until 'output' is completely filled.
 */
template <typename CharT>
size_t ReadChunk(ByteStream& stream, BasicBufferSpan<CharT> output, std::error_code& ec)
{
    uint64_t processed = 0;

    while (processed != output.size())
    {
        BufferSpan span(
            reinterpret_cast<uint8_t*>(output.data() + processed), output.size() * sizeof(CharT) - processed);

        const auto lastReadSize = Read(stream, span, ec);
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
template <typename ContainerT>
size_t Read(ByteStream& stream, ContainerT& output, std::error_code& ec)
{
    BufferSpan span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(ContainerT::value_type));

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
template <typename ContainerT>
size_t ReadChunk(ByteStream& stream, ContainerT& output, std::error_code& ec)
{
    BufferSpan span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(ContainerT::value_type));
    return ReadChunk(stream, span, ec);
}

/*!
 * \brief Read at specified position the available bytes and store them in container of type ContainerT
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename ContainerT>
size_t ReadAt(ByteStream& stream, uint64_t offset, ContainerT& output, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
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
template <typename ContainerT>
size_t ReadChunkAt(ByteStream& stream, uint64_t offset, ContainerT& output, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    BufferSpan span(reinterpret_cast<uint8_t*>(output.data()), output.size() * sizeof(ContainerT::value_type));
    return ReadChunk(stream, span, ec);
}

/*!
 * \brief Read at specified position in 'stream' until 'output' is completely filled.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename CharT>
size_t
ReadChunkAt(ByteStream& stream, uint64_t offset, size_t chunkSizeCb, BasicBufferSpan<CharT> output, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    assert(chunkSizeCb < output.size() * sizeof(CharT) && "Buffer 'output' overflow");

    BufferSpan span(reinterpret_cast<uint8_t*>(output.data()), chunkSizeCb);
    return ReadChunk(stream, span, ec);
}

/*!
 * \brief Fill an item of type 'T' and fail if there is not enough data to read.
 */
template <typename ItemT>
void ReadItem(ByteStream& stream, ItemT& output, std::error_code& ec)
{
    uint64_t processed = Read(stream, BufferSpan(reinterpret_cast<uint8_t*>(&output), sizeof(output)), ec);
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
template <typename ItemT>
void ReadItemAt(ByteStream& stream, uint64_t offset, ItemT& output, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return;
    }

    ReadItem(stream, output, ec);
}

/*!
 * \brief Write 'input' into 'output' stream.
 */
template <typename CharT>
size_t Write(ByteStream& stream, BasicBufferView<CharT> input, std::error_code& ec)
{
    uint64_t cbWritten = 0;

    HRESULT hr = stream.Write(const_cast<uint8_t*>(input.data()), input.size() * sizeof(CharT), &cbWritten);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to write stream [{}]", ec);
        return cbWritten;
    }

    return cbWritten;
}

/*!
 * \brief Write fully 'input' into output 'stream'.
 */
template <typename CharT>
size_t WriteChunk(ByteStream& stream, BasicBufferView<CharT> input, std::error_code& ec)
{
    uint64_t processed = 0;

    while (processed != input.size())
    {
        // TODO: subview
        BufferView span(
            reinterpret_cast<const uint8_t*>(input.data() + processed), input.size() * sizeof(CharT) - processed);

        const auto lastWriteSize = Write(stream, span, ec);
        if (ec)
        {
            return processed;
        }

        if (lastWriteSize == 0)
        {
            ec = std::make_error_code(std::errc::io_error);
            return processed;
        }

        processed += lastWriteSize;
    }

    return processed;
}

/*!
 * \brief Write 'input' into output 'stream'.
 */
template <typename ContainerT>
size_t Write(ByteStream& stream, const ContainerT& input, std::error_code& ec)
{
    BufferView span(reinterpret_cast<const uint8_t*>(input.data()), input.size() * sizeof(ContainerT::value_type));

    size_t processed = stream.Write(span, ec);
    if (ec)
    {
        return processed;
    }

    stream.resize(processed / sizeof(ContainerT::value_type));
    return processed;
}

/*!
 * \brief Write at specified position in the output 'stream'.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename ContainerT>
size_t WriteAt(ByteStream& stream, uint64_t offset, const ContainerT& input, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    return Write(stream, input, ec);
}

/*!
 * \brief Read at specified position in 'stream' until 'output' is completely filled.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename ContainerT>
size_t WriteChunkAt(ByteStream& stream, uint64_t offset, const ContainerT& input, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    BufferView span(reinterpret_cast<uint8_t*>(input.data()), input.size() * sizeof(ContainerT::value_type));
    return WriteChunk(stream, span, ec);
}

/*!
 * \brief Write at specified position in 'stream' until 'output' is completely filled.
 *
 * TODO: C++20: use std::is_contiguous_iterator
 */
template <typename CharT>
size_t
WriteChunkAt(ByteStream& stream, uint64_t offset, size_t chunkSizeCb, BasicBufferView<CharT> input, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return 0;
    }

    assert(chunkSizeCb < output.size() * sizeof(CharT) && "Buffer 'output' overflow");

    BufferSpan span(reinterpret_cast<uint8_t*>(input.data()), chunkSizeCb);
    return WriteChunk(stream, span, ec);
}

/*!
 * \brief Write fully an item of type 'T'.
 */
template <typename ItemT>
void WriteItem(ByteStream& stream, const ItemT& input, std::error_code& ec)
{
    uint64_t processed = Write(stream, BufferView(reinterpret_cast<const uint8_t*>(&output), sizeof(output)), ec);
    if (ec)
    {
        return;
    }

    if (processed != sizeof(output))
    {
        ec = std::make_error_code(std::errc::interrupted);
        Log::Debug("Failed to write expected size ({}/{})", processed, sizeof(ItemT));
        return;
    }
}

/*!
 * \brief Write full an item of type 'ItemT' at specified offset.
 */
template <typename ItemT>
void WriteItemAt(ByteStream& stream, uint64_t offset, ItemT& output, std::error_code& ec)
{
    ULONG64 currPointer = 0;
    HRESULT hr = stream.SetFilePointer(offset, FILE_BEGIN, &currPointer);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to seek to position: {} [{}]", Traits::Offset(offset), ec);
        return;
    }

    WriteItem(stream, output, ec);
}

}  // namespace Orc
