//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Stream/StreamReader.h"
#include "Filesystem/Ntfs/ShadowCopy/ShadowCopy.h"

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class ShadowCopyStream final : public StreamReader
{
public:
    using Ptr = std::shared_ptr<ShadowCopyStream>;

    ShadowCopyStream(StreamReader::Ptr stream, const GUID& shadowCopyId, std::error_code& ec);

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec) override;
    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec) override;

    const ShadowCopy& ShadowCopy() const { return m_shadowCopy; }

private:
    uint64_t m_pos;
    StreamReader::Ptr m_stream;
    Ntfs::ShadowCopy::ShadowCopy m_shadowCopy;
};

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
