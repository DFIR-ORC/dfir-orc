//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Stream/StreamReader.h"

namespace Orc {

class VolumeReader;

namespace Stream {

class VolumeStreamReader final : public StreamReader
{
public:
    explicit VolumeStreamReader(std::shared_ptr<VolumeReader> stream);

    size_t Read(gsl::span<uint8_t> output, std::error_code& ec) override;
    uint64_t Seek(SeekDirection direction, int64_t value, std::error_code& ec) override;

private:
    std::shared_ptr<VolumeReader> m_stream;
};

}  // namespace Stream

using VolumeStreamReader = Stream::VolumeStreamReader;

}  // namespace Orc
