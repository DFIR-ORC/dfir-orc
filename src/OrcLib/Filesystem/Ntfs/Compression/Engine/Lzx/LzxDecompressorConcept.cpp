//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "LzxDecompressorConcept.h"

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxSlots.h"
#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

namespace Orc {

size_t LzxDecompressorConcept::Decompress(BufferView input, gsl::span<uint8_t> output, std::error_code& ec)
{
    if (input.empty() || output.empty() || output.size() > Ntfs::Lzx::kWindowSize)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        Log::Debug("Lzx: refusing {} compressed bytes into {} of output [{}]", input.size(), output.size(), ec);
        return 0;
    }

    return m_decoder.Decode(input, output, ec);
}

}  // namespace Orc
