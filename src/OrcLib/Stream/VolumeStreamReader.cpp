//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "VolumeStreamReader.h"

#include "VolumeReader.h"

namespace Orc {
namespace Stream {

VolumeStreamReader::VolumeStreamReader(std::shared_ptr<VolumeReader> stream)
    : m_stream(stream)
{
}

size_t VolumeStreamReader::Read(gsl::span<uint8_t> output, std::error_code& ec)
{
    ULONGLONG processed = 0;
    HRESULT hr = m_stream->Read(output.data(), output.size(), processed);
    if (FAILED(hr))
    {
        ec.assign(hr, std::system_category());
        Log::Debug("Failed to read from volume stream [{}]", ec);
        return processed;
    }

    return processed;
}

uint64_t VolumeStreamReader::Seek(SeekDirection direction, int64_t value, std::error_code& ec)
{
    if (direction != SeekDirection::kBegin)
    {
        Log::Debug("Failed to seek volume reader to {:#x}: invalid seek direction", value);
        ec = std::make_error_code(std::errc::operation_not_supported);
        return value;
    }

    HRESULT hr = m_stream->Seek(value);
    if (FAILED(hr))
    {
        ec.assign(hr, std::system_category());
        Log::Debug("Failed to seek volume reader to {:#x} [{}]", value, ec);
        return value;
    }

    return value;
}

}  // namespace Stream
}  // namespace Orc
