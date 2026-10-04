//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include <gsl/span>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxChunkDecoder.h"
#include "Utils/BufferView.h"

namespace Orc {

// The DecompressorConcept face of the LZX engine, mirroring NtDecompressorConcept so WofStreamConcept accepts
// either. Needs no system API, so it works on every Windows version ORC builds for.
class LzxDecompressorConcept
{
public:
    LzxDecompressorConcept() = default;

    // Decodes one chunk into 'output', whose size is the chunk's uncompressed size. Returns the bytes produced,
    // which is 'output.size()' on success and 0 with 'ec' set otherwise.
    [[nodiscard]] size_t Decompress(BufferView input, gsl::span<uint8_t> output, std::error_code& ec);

private:
    Ntfs::Lzx::ChunkDecoder m_decoder;
};

}  // namespace Orc
