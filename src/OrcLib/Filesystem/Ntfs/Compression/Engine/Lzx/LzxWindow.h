//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>
#include <system_error>

#include <gsl/span>

namespace Orc::Ntfs::Lzx {

// Emits literals and matches into the caller's output buffer. A WOF chunk decodes independently, so the history
// a match may reach is exactly the bytes already produced here; there is no window carried in from a previous
// chunk and nothing to copy between buffers.
class Window
{
public:
    explicit Window(gsl::span<uint8_t> output)
        : m_output(output)
    {
    }

    [[nodiscard]] bool WriteLiteral(uint8_t value, std::error_code& ec);
    [[nodiscard]] bool WriteMatch(uint32_t offset, uint32_t length, std::error_code& ec);

    [[nodiscard]] size_t Produced() const noexcept { return m_produced; }
    [[nodiscard]] size_t Remaining() const noexcept { return m_output.size() - m_produced; }

private:
    gsl::span<uint8_t> m_output;
    size_t m_produced = 0;
};

}  // namespace Orc::Ntfs::Lzx
