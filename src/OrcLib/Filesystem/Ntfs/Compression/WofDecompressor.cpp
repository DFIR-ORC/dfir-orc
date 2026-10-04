//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WofDecompressor.h"

#include <algorithm>

#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"

namespace {

using namespace Orc;
using namespace Orc::Ntfs;

std::underlying_type_t<WofAlgorithm> AlgorithmId(WofAlgorithm algorithm)
{
    return static_cast<std::underlying_type_t<WofAlgorithm>>(algorithm);
}

}  // namespace

namespace Orc::Ntfs {

WofDecompressor::WofDecompressor(Engine engine)
    : m_engine(std::move(engine))
{
}

Result<WofDecompressor> WofDecompressor::Create(WofAlgorithm algorithm)
{
    std::error_code ec;

    switch (algorithm)
    {
        case WofAlgorithm::kXpress4k:
        case WofAlgorithm::kXpress8k:
        case WofAlgorithm::kXpress16k: {
            NtDecompressorConcept engine(algorithm, ec);
            if (ec)
            {
                Log::Debug("Wof: no nt decompressor for algorithm id {} [{}]", AlgorithmId(algorithm), ec);
                return ec;
            }

            return WofDecompressor(Engine(std::move(engine)));
        }
        case WofAlgorithm::kLzx:
            return WofDecompressor(Engine(LzxDecompressorConcept()));
        case WofAlgorithm::kUnknown:
            break;
    }

    // 'ToWofAlgorithm' already rejects an unknown id at parse time; this stays as the fail closed default.
    ec = std::make_error_code(std::errc::operation_not_supported);
    Log::Debug("Wof: no decompressor for algorithm id {} [{}]", AlgorithmId(algorithm), ec);
    return ec;
}

size_t WofDecompressor::Decompress(BufferView input, gsl::span<uint8_t> output, std::error_code& ec)
{
    // A chunk Microsoft chose not to compress is stored raw, and WOF signals that only by the size equality.
    // Handled here so both engines inherit it from one place.
    if (input.size() == output.size())
    {
        std::copy(input.begin(), input.end(), output.begin());
        return output.size();
    }

    return std::visit([&input, &output, &ec](auto& engine) { return engine.Decompress(input, output, ec); }, m_engine);
}

}  // namespace Orc::Ntfs
