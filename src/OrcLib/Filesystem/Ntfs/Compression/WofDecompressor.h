//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>
#include <variant>

#include <gsl/span>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxDecompressorConcept.h"
#include "Filesystem/Ntfs/Compression/Engine/Nt/NtDecompressorConcept.h"
#include "Filesystem/Ntfs/Compression/WofAlgorithm.h"
#include "Utils/BufferView.h"
#include "Utils/Result.h"

namespace Orc::Ntfs {

// The one place a WOF decompression engine is chosen. 'WofStreamConcept' holds this through 'MetaPtr' and never
// learns which engine is behind it, so XPRESS (Windows' Rtl API) and LZX (OrcLib's own decoder) share one stream.
class WofDecompressor
{
public:
    [[nodiscard]] static Result<WofDecompressor> Create(WofAlgorithm algorithm);

    // Decodes one chunk into 'output', sized to the chunk's uncompressed size. A chunk stored raw (compressed size
    // equal to the uncompressed size, WOF's only signal for it) is copied, whichever engine is selected.
    [[nodiscard]] size_t Decompress(BufferView input, gsl::span<uint8_t> output, std::error_code& ec);

private:
    using Engine = std::variant<NtDecompressorConcept, LzxDecompressorConcept>;

    explicit WofDecompressor(Engine engine);

    Engine m_engine;
};

}  // namespace Orc::Ntfs
