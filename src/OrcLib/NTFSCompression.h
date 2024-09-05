//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <stdint.h>

#pragma managed(push, off)

namespace Orc {

/* Variables used for ntfs_uncompress() method */
struct NTFS_COMP_INFO
{
    char* uncomp_buf;  // Buffer for uncompressed data
    char* comp_buf;  // buffer for compressed data
    size_t comp_len;  // number of bytes used in compressed data
    size_t uncomp_idx;  // Index into buffer for next byte
    size_t buf_size_b;  // size of buffer in bytes (1 compression unit)
};

HRESULT ntfs_uncompress_reset(NTFS_COMP_INFO* comp);
HRESULT ntfs_uncompress_setup(unsigned int block_size, NTFS_COMP_INFO* comp, UINT compunit_size_c);

HRESULT ntfs_uncompress_done(NTFS_COMP_INFO* comp);

// SleuthKit
HRESULT ntfs_uncompress_compunit(NTFS_COMP_INFO* comp);

// NTFS-3G
HRESULT ntfs_decompress(uint8_t* dest, const size_t dest_size, uint8_t* const cb_start, const size_t cb_size);

}  // namespace Orc

#pragma managed(pop)
