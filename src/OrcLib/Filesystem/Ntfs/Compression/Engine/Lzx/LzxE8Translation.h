//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstddef>
#include <cstdint>

#include <gsl/span>

namespace Orc::Ntfs::Lzx {

// The WOF dialect of LZX always applies x86 call translation, with this fixed file size and no header bit saying
// so. Established against the known plaintext fixture: decoding without this step diverges at the first 0xE8 byte.
constexpr uint32_t kE8FileSize = 12000000;

// The last bytes of a chunk are never translated: an operand there could run past the end.
constexpr size_t kE8Tail = 10;

// Undoes the encoder's E8 rewrite in place. Positions are relative to the chunk, which is what resets per chunk.
void UndoE8Translation(gsl::span<uint8_t> chunk);

}  // namespace Orc::Ntfs::Lzx
