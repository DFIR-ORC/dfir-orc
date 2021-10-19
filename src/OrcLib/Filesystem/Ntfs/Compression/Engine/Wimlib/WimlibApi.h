//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>
#include <system_error>

#include "Filesystem/Ntfs/Compression/Engine/Wimlib/WimlibErrorCategory.h"

//
// From: wimlib/include/wimlib.h
//

enum wimlib_compression_type
{
    WIMLIB_COMPRESSION_TYPE_NONE = 0,
    WIMLIB_COMPRESSION_TYPE_XPRESS = 1,
    WIMLIB_COMPRESSION_TYPE_LZX = 2,
    WIMLIB_COMPRESSION_TYPE_LZMS = 3
};

using wimlib_tchar = wchar_t;

namespace Orc {

void wimlib_set_default_compression_level(int ctype, unsigned int compression_level, std::error_code& ec);

uint64_t wimlib_get_compressor_needed_memory(
    enum wimlib_compression_type ctype,
    size_t max_block_size,
    unsigned int compression_level,
    std::error_code& ec);

void wimlib_create_compressor(
    enum wimlib_compression_type ctype,
    size_t max_block_size,
    unsigned int compression_level,
    struct wimlib_compressor** compressor_ret,
    std::error_code& ec);

size_t wimlib_compress(
    const void* uncompressed_data,
    size_t uncompressed_size,
    void* compressed_data,
    size_t compressed_size_avail,
    struct wimlib_compressor* compressor,
    std::error_code& ec);

void wimlib_free_compressor(struct wimlib_compressor* compressor, std::error_code& ec);

void wimlib_create_decompressor(
    enum wimlib_compression_type ctype,
    size_t max_block_size,
    struct wimlib_decompressor** decompressor_ret,
    std::error_code& ec);

void wimlib_decompress(
    const void* compressed_data,
    size_t compressed_size,
    void* uncompressed_data,
    size_t uncompressed_size,
    struct wimlib_decompressor* decompressor,
    std::error_code& ec);

void wimlib_free_decompressor(struct wimlib_decompressor* decompressor, std::error_code& ec);

const wimlib_tchar* wimlib_get_error_string(wimlib_errc code, std::error_code& ec);

}  // namespace Orc
