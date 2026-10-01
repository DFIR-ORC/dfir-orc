//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#pragma once

#include "Utils/BufferView.h"
#include "Utils/Result.h"

#include <array>
#include <cstdint>

namespace Orc {

// HMAC-SHA256 of 'data' under 'key'. An empty key is an error: it is never a legitimate input here,
// and accepting it would produce a digest that looks valid while authenticating nothing.
//
// Backed by CNG, whose entry points are resolved at run time (see Crypto/BCryptApi.h), so on a host
// without bcrypt.dll - Windows XP - this returns an error rather than failing to load the binary.
//
// CNG rather than the CryptoAPI the rest of OrcLib hashes with: CryptoHashStream already offers SHA-256
// but no MAC, and the CryptoAPI route to one is to import the key as a PLAINTEXTKEYBLOB pretending to be
// an RC2 key with CRYPT_IPSEC_HMAC_KEY. CNG takes the key as bytes. SigV4 chains five of these per
// request, so that indirection would be paid five times over for a primitive CNG provides directly.
Result<std::array<uint8_t, 32>> HmacSha256(BufferView key, BufferView data);

}  // namespace Orc
