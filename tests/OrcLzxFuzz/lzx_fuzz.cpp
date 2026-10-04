//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
// libFuzzer harness for the LZX chunk decoder. The decoder must return for any input: it may refuse, it may
// decode, it may not read or write out of bounds, which is what AddressSanitizer checks. The first input byte
// also picks the output size, so the short last chunk paths are exercised, not only a full 32768 byte window.
//
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>

#include <gsl/span>

#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxDecompressorConcept.h"
#include "Filesystem/Ntfs/Compression/Engine/Lzx/LzxSlots.h"
#include "Utils/BufferView.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0)
    {
        return 0;
    }

    // 32768 for a leading 0x00, down to 128 for 0xFF: every size is a legal chunk size for the window.
    const size_t outputSize = Orc::Ntfs::Lzx::kWindowSize - static_cast<size_t>(data[0]) * 128;

    std::vector<uint8_t> output(outputSize, 0);
    Orc::LzxDecompressorConcept decompressor;
    std::error_code ec;
    static_cast<void>(decompressor.Decompress(Orc::BufferView(data, size), gsl::make_span(output), ec));
    return 0;
}
