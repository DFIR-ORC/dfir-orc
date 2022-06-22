//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <system_error>

#include "Filesystem/Ntfs/Compression/Engine/Nt/NtAlgorithm.h"
#include "Filesystem/Ntfs/Compression/Engine/Wimlib/WimlibAlgorithm.h"

namespace Orc {

enum class NtAlgorithm;

namespace Ntfs {

// See FILE_PROVIDER_COMPRESSION_* in 'wofapi.h' for those enum value
enum class WofAlgorithm
{
    kUnknown = -1,
    kXpress4k = 0,
    kLzx = 1,
    kXpress8k = 2,
    kXpress16k = 3
};

WofAlgorithm ToWofAlgorithm(const std::string& algorithm, std::error_code& ec);

NtAlgorithm ToNtAlgorithm(WofAlgorithm algorithm);

WofAlgorithm ToWofAlgorithm(uint32_t algorithm, std::error_code& ec);

WofAlgorithm ToWofAlgorithm(NtAlgorithm algorithm, uint64_t chunkSize, std::error_code& ec);

WofAlgorithm ToWofAlgorithm(WimlibAlgorithm algorithm, uint64_t chunkSize, std::error_code& ec);

}  // namespace Ntfs

std::string_view ToString(Ntfs::WofAlgorithm algorithm);

}  // namespace Orc
