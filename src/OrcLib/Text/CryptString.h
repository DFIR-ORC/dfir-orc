//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>

#include <windows.h>

#include "Utils/BufferView.h"
#include "Utils/Result.h"

namespace Orc {

// CryptBinaryToStringW with 'format' | CRYPT_STRING_NOCRLF. XP and 2003 do not support that flag: when the call
// fails, the text comes from BinaryToSingleLineString instead. Empty input gives an empty string.
Result<std::wstring> CryptBinaryToSingleLineString(BufferView data, DWORD format);

// The fallback: what CryptBinaryToStringW gives with 'format' | CRYPT_STRING_NOCRLF, built without the flag. Two
// formats are supported, the ones the code base uses:
// - CRYPT_STRING_BASE64: encoded without the flag, then the CR/LF CryptoAPI inserts are removed (base64 text never
//   contains them);
// - CRYPT_STRING_HEX: formatted here as lowercase byte pairs separated by one space, since without the flag
//   CryptoAPI also changes the spacing, not only the line breaks.
// Empty input gives an empty string; any other format is refused.
Result<std::wstring> BinaryToSingleLineString(BufferView data, DWORD format);

}  // namespace Orc
