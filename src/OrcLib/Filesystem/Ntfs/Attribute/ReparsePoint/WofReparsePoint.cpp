//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WofReparsePoint.h"

#include "Log/Log.h"

namespace Orc {
namespace Ntfs {

Result<WofReparsePoint> WofReparsePoint::Parse(BufferView buffer)
{
    if (buffer.size() < sizeof(WofReparsePoint::Layout))
    {
        Log::Debug("Failed to parse wof reparse point: invalid size");
        return std::make_error_code(std::errc::message_size);
    }

    return WofReparsePoint(*reinterpret_cast<const WofReparsePoint::Layout*>(buffer.data()));
}

Result<WofReparsePoint> WofReparsePoint::Parse(WofReparsePoint::Layout& layout)
{
    if (layout.version != 1)
    {
        Log::Debug("Failed to parse wof reparse point: unsupported version (version: {})", layout.version);
        return std::make_error_code(std::errc::bad_message);
    }

    if (layout.providerVersion != 1)
    {
        Log::Debug(
            "Failed to parse wof reparse point: unsupported provider version (version: {})", layout.providerVersion);
        return std::make_error_code(std::errc::bad_message);
    }

    if (layout.provider != std::underlying_type_t<WofProviderType>(WofProviderType::kFile))
    {
        Log::Debug("Failed to parse wof reparse point: unsupported provider (version: {})", layout.provider);
        return std::make_error_code(std::errc::bad_message);
    }

    std::error_code ec;
    const auto algorithm = ToWofAlgorithm(layout.compressionFormat, ec);
    if (ec)
    {
        Log::Debug("Failed while reading wof algorithm [{}]", ec);
        return ec;
    }

    if (algorithm == WofAlgorithm::kLzx)
    {
        Log::Debug("Unsupported wof algorithm [{}]", ec);
        return ec;
    }

    return WofReparsePoint(layout);
}

WofReparsePoint::WofReparsePoint(const WofReparsePoint::Layout& header)
{
    m_version = header.version;
    m_provider = static_cast<WofProviderType>(header.provider);
    m_providerVersion = header.providerVersion;
    m_compressionFormat = static_cast<WofAlgorithm>(header.compressionFormat);
}

uint32_t WofReparsePoint::Version() const
{
    return m_version;
}

WofProviderType WofReparsePoint::Provider() const
{
    return m_provider;
}

uint32_t WofReparsePoint::ProviderVersion() const
{
    return m_providerVersion;
}

WofAlgorithm WofReparsePoint::CompressionFormat() const
{
    return m_compressionFormat;
}

}  // namespace Ntfs
}  // namespace Orc
