//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author: Pierre Chifflier <pierre.chifflier@ssi.gouv.fr>
//         Raphaël Rigo <raphael.rigo@ssi.gouv.fr>

#pragma once

#include <windows.h>

#include <stdint.h>

#pragma managed(push, off)

namespace Orc {

static const auto DIR_CERT = 4;

typedef enum _PE_SIZE
{
    PE32,
    PE64
} PE_SIZE;

typedef struct _PE_IMAGE64
{
    IMAGE_FILE_HEADER* hdr_pe;
    IMAGE_OPTIONAL_HEADER64* hdr_optpe;
} PE_IMAGE64, *PPE_IMAGE64;

typedef struct _PE_IMAGE32
{
    IMAGE_FILE_HEADER* hdr_pe;
    IMAGE_OPTIONAL_HEADER32* hdr_optpe;
} PE_IMAGE32, *PPE_IMAGE32;

typedef struct _PE_IMAGE
{
    PE_SIZE size;
    int32_t mPeCoffHeaderOffset;
    union
    {
        PE_IMAGE32 img32;
        PE_IMAGE64 img64;
    };
} PE_IMAGE, *PPE_IMAGE;

int parse_pe(const unsigned char* data, size_t datalen, struct _PE_IMAGE* pe);
PIMAGE_DATA_DIRECTORY pe_get_secdir(struct _PE_IMAGE* pe);
uint32_t pe_get_checksum_offset(PPE_IMAGE pe);
uint32_t pe_get_sizeof_headers(PPE_IMAGE pe);

int calc_pe_chunks_spec(unsigned char* in, size_t size, uint32_t* chunks, size_t nchunks);
int calc_pe_chunks_real(unsigned char* in, size_t size, uint32_t* chunks, size_t nchunks);

}  // namespace Orc
#pragma managed(pop)
