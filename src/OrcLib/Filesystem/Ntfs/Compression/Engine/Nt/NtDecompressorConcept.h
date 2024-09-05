//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include "Utils/BufferView.h"

#include "Filesystem/Ntfs/Compression/WofAlgorithm.h"
#include "Filesystem/Ntfs/Compression/Engine/Nt/NtAlgorithm.h"

namespace Orc {

class NtDecompressorConcept
{
public:
    // Required by ByteStream interface
    NtDecompressorConcept() = default;

    NtDecompressorConcept(Ntfs::WofAlgorithm algorithm, std::error_code& ec);
    NtDecompressorConcept(NtAlgorithm algorithm, std::error_code& ec);

    size_t Decompress(BufferView input, gsl::span<uint8_t> output, std::error_code& ec);

private:
    NtAlgorithm m_ntAlgorithm;
    std::vector<uint8_t> m_workspace;  // Usually ~160k
};

}  // namespace Orc
