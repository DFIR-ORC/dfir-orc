//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author: Pierre Chifflier <pierre.chifflier@ssi.gouv.fr>
//         Raphaël Rigo <raphael.rigo@ssi.gouv.fr>
//

#include "stdafx.h"

#include "libpehash-pe.h"

#include <string.h>

using namespace Orc;

constexpr auto GET_UINT16_LE(uint8_t* p)
{
    return p[0] | (p[1] << 8);
}

constexpr auto GET_UINT32_LE(uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

constexpr void PUT_UINT32_LE(uint32_t i, u_char* p)
{
    p[0] = (i)&0xff;
    p[1] = (i >> 8) & 0xff;
    p[2] = (i >> 16) & 0xff;
    p[3] = (i >> 24) & 0xff;
}

int Orc::parse_pe(const unsigned char* data, size_t datalen, struct _PE_IMAGE* pe)
{
    uint32_t peheader_loc;
    IMAGE_FILE_HEADER* hdr_pe;

    if (data == NULL)
    {
        // fprintf(stderr, "Refusing to parse NULL pointer\n");
        return -1;
    }

    if (datalen < 64)
    {
        // fprintf(stderr, "Image is too small to have a COFF header\n");
        return -1;
    }
    if (memcmp(data, "MZ", 2))
    {
        // fprintf(stderr, "Not a COFF header\n");
        return -1;
    }

    /* COFF header */
    peheader_loc = GET_UINT32_LE((uint8_t*)data + 60);
    if (datalen < (peheader_loc + sizeof(IMAGE_FILE_HEADER)))
    {
        // fprintf(stderr, "Image is too small to have a PE header\n");
        return -1;
    }

    /* PE header */
    if (memcmp(data + peheader_loc, "PE\0\0", 4))
    {
        // fprintf(stderr, "Not a PE header\n");
        return -1;
    }

    hdr_pe = (IMAGE_FILE_HEADER*)(data + peheader_loc + 4);

    if (datalen < (peheader_loc + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64)))
    {
        // fprintf(stderr, "Image is too small to have an Optional PE header\n");
        return -1;
    }

    pe->mPeCoffHeaderOffset = peheader_loc + 4;
    /* Check if PE32 or PE64 */
    if (memcmp((uint8_t*)hdr_pe + sizeof(IMAGE_FILE_HEADER), "\x0b\x01", 2) == 0)
    {
        /* PE32 */
        pe->size = PE32;
        /* (not-so) optional PE header */
        pe->img32.hdr_pe = hdr_pe;
        pe->img32.hdr_optpe = (IMAGE_OPTIONAL_HEADER32*)((uint8_t*)hdr_pe + sizeof(IMAGE_FILE_HEADER));
    }
    else
    {
        /* PE64 */
        pe->size = PE64;
        /* (not-so) optional PE header */
        pe->img64.hdr_pe = hdr_pe;
        pe->img64.hdr_optpe = (IMAGE_OPTIONAL_HEADER64*)((uint8_t*)hdr_pe + sizeof(IMAGE_FILE_HEADER));
    }

    return 0;
}

/* Return the Security Directory, independently of the PE version */
PIMAGE_DATA_DIRECTORY Orc::pe_get_secdir(struct _PE_IMAGE* pe)
{
    if (pe->size == PE32)
        return &(pe->img32.hdr_optpe->DataDirectory[DIR_CERT]);
    else
        return &(pe->img64.hdr_optpe->DataDirectory[DIR_CERT]);
}

/* Returns offset of the Checksum field from the IMAGE_FILE_HEADER */
uint32_t Orc::pe_get_checksum_offset(PPE_IMAGE pe)
{
    if (pe->size == PE32)
        return (uint32_t)(
            (unsigned char*)&(pe->img32.hdr_optpe->CheckSum) - (unsigned char*)(pe->img32.hdr_optpe)
            + sizeof(IMAGE_FILE_HEADER));
    else
        return (uint32_t)(
            (unsigned char*)&(pe->img64.hdr_optpe->CheckSum) - (unsigned char*)(pe->img64.hdr_optpe)
            + sizeof(IMAGE_FILE_HEADER));
}

uint32_t Orc::pe_get_sizeof_headers(PPE_IMAGE pe)
{
    if (pe->size == PE32)
        return pe->img32.hdr_optpe->SizeOfHeaders;
    else
        return pe->img64.hdr_optpe->SizeOfHeaders;
}

/* Computes the different chunks needed to hash the PE
 * Implements the specification
 * @in : PE mapping
 * @size : size of the PE File
 * @chunks : pointer to an array containing 2*@nchunks uint32_t
 *           the resulting data is [offset, size]
 * @nchunks : number of allocated chunks
 * return value : number of computed chunks
 */
int Orc::calc_pe_chunks_spec(unsigned char* in, size_t size, uint32_t* chunks, size_t nchunks)
{
    PIMAGE_DATA_DIRECTORY secdir;
    PE_IMAGE pe_img;
    IMAGE_FILE_HEADER* pe;
    int curchunk = 0, rc = 1;

    if (in == NULL || chunks == NULL || nchunks < 4)
        return -1;

    rc = parse_pe(in, size, &pe_img);
    if (rc < 0)
    {
        return -1;
    }

    pe = pe_img.img64.hdr_pe;

    /* Check we have enough chunks to store results */
    if (nchunks < (size_t)(4 + pe->NumberOfSections))
        return -2;

    /* XXX read security (certificates) directory */
    secdir = pe_get_secdir(&pe_img);

    /********* hash PE file *********/

    uint8_t* mImageBase = in;
    uint32_t mImageSize = (uint32_t)size;
    uint8_t* HashBase;
    uint32_t HashSize;
    uint32_t SumOfBytesHashed;
    uint32_t SumOfSectionBytes;
    uint32_t Pos;
    uint32_t Index;
    IMAGE_SECTION_HEADER* Section;
    IMAGE_SECTION_HEADER* SectionCache;
    IMAGE_SECTION_HEADER* SectionHeader;

    // 1.  Load the image header into memory.

    // 2.  Initialize a SHA hash context.

    //
    // Measuring PE/COFF Image Header;
    // But CheckSum field and SECURITY data directory (certificate) are excluded
    //
    // 3.  Calculate the distance from the base of the image header to the image checksum address.
    // 4.  Hash the image header from its base to beginning of the image checksum.
    //
    HashBase = mImageBase;
    HashSize = pe_img.mPeCoffHeaderOffset + pe_get_checksum_offset(&pe_img);
    chunks[curchunk * 2] = (uint32_t)(HashBase - in);
    chunks[curchunk * 2 + 1] = HashSize;
    curchunk++;

    //
    // 5.  Skip over the image checksum (it occupies a single ULONG).
    // 6.  Get the address of the beginning of the Cert Directory.
    // 7.  Hash everything from the end of the checksum to the start of the Cert Directory.
    //
    HashBase = mImageBase + pe_img.mPeCoffHeaderOffset + pe_get_checksum_offset(&pe_img) + sizeof(uint32_t);
    HashSize = (uint32_t)((unsigned char*)secdir - HashBase);
    chunks[curchunk * 2] = (uint32_t)(HashBase - in);
    chunks[curchunk * 2 + 1] = HashSize;
    curchunk++;

    //
    // 8.  Skip over the Cert Directory. (It is sizeof(IMAGE_DATA_DIRECTORY) bytes.)
    // 9.  Hash everything from the end of the Cert Directory to the end of image header.
    //
    HashBase = (uint8_t*)(secdir) + sizeof(IMAGE_DATA_DIRECTORY);
    HashSize = (uint32_t)(
        pe_get_sizeof_headers(&pe_img) - ((unsigned char*)secdir - mImageBase) - sizeof(IMAGE_DATA_DIRECTORY));
    chunks[curchunk * 2] = (uint32_t)(HashBase - in);
    chunks[curchunk * 2 + 1] = HashSize;
    curchunk++;

    //
    // 10. Set the SUM_OF_BYTES_HASHED to the size of the header.
    //
    SumOfBytesHashed = pe_get_sizeof_headers(&pe_img);

    Section =
        (IMAGE_SECTION_HEADER*)(mImageBase + pe_img.mPeCoffHeaderOffset + sizeof(IMAGE_FILE_HEADER) + pe->SizeOfOptionalHeader);

    SectionCache = Section;
    for (Index = 0, SumOfSectionBytes = 0; Index < pe->NumberOfSections; Index++, SectionCache++)
    {
        SumOfSectionBytes += SectionCache->SizeOfRawData;
    }

    //
    // Sanity check for file corruption. Sections raw data size should be smaller
    // than Image Size.
    //
    if (SumOfSectionBytes > mImageSize)
    {
        // printf("Sections raw data are bigger than Image size ! : 0x%x > 0x%x\n", SumOfSectionBytes, mImageSize);
        return -1;
    }

    //
    // 11. Build a temporary table of pointers to all the IMAGE_SECTION_HEADER
    //     structures in the image. The 'NumberOfSections' field of the image
    //     header indicates how big the table should be. Do not include any
    //     IMAGE_SECTION_HEADERs in the table whose 'SizeOfRawData' field is zero.
    //
    SectionHeader = (IMAGE_SECTION_HEADER*)calloc(sizeof(IMAGE_SECTION_HEADER), pe->NumberOfSections);
    if (SectionHeader == NULL)
    {
        // printf("Could not allocate memory\n");
        return -2;
    }

    //
    // 12.  Using the 'PointerToRawData' in the referenced section headers as
    //      a key, arrange the elements in the table in ascending order. In other
    //      words, sort the section headers according to the disk-file offset of
    //      the section.
    //
    for (Index = 0; Index < pe->NumberOfSections; Index++)
    {
        Pos = Index;
        while ((Pos > 0) && (Section->PointerToRawData < SectionHeader[Pos - 1].PointerToRawData))
        {
            memcpy(&SectionHeader[Pos], &SectionHeader[Pos - 1], sizeof(IMAGE_SECTION_HEADER));
            Pos--;
        }
        memcpy(&SectionHeader[Pos], Section, sizeof(IMAGE_SECTION_HEADER));
        Section += 1;
    }

    //
    // 13.  Walk through the sorted table, bring the corresponding section
    //      into memory, and hash the entire section (using the 'SizeOfRawData'
    //      field in the section header to determine the amount of data to hash).
    // 14.  Add the section's 'SizeOfRawData' to SUM_OF_BYTES_HASHED .
    // 15.  Repeat steps 13 and 14 for all the sections in the sorted table.
    //
    for (Index = 0; Index < pe->NumberOfSections; Index++)
    {
        Section = &SectionHeader[Index];
        if (Section->SizeOfRawData == 0)
        {
            continue;
        }
        HashBase = mImageBase + Section->PointerToRawData;
        HashSize = (uint32_t)Section->SizeOfRawData;
        chunks[curchunk * 2] = (uint32_t)(HashBase - in);
        chunks[curchunk * 2 + 1] = HashSize;
        curchunk++;

        SumOfBytesHashed += HashSize;
    }

    free(SectionHeader);

    //
    // 16.  If the file size is greater than SUM_OF_BYTES_HASHED, there is extra
    //      data in the file that needs to be added to the hash. This data begins
    //      at file offset SUM_OF_BYTES_HASHED and its length is:
    //             FileSize  -  (CertDirectory->Size)
    //
    if (mImageSize > SumOfBytesHashed)
    {
        HashBase = mImageBase + SumOfBytesHashed;
        HashSize = (uint32_t)(mImageSize - secdir->Size - SumOfBytesHashed);
        chunks[curchunk * 2] = (uint32_t)(HashBase - in);
        chunks[curchunk * 2 + 1] = HashSize;
        curchunk++;
    }

    /********* end hash PE file *********/

    return curchunk;
}

/* Computes the different chunks needed to hash the PE
 * Implements the same algo as found in signtool
 * IMPORTANT NOTE :
 * 	you have to pad the input with zeros to make the
 * 	size a multiple of 8 (yes, this is not documented,
 * 	anywhere).
 * @in : PE mapping
 * @size : size of the PE File
 * @chunks : pointer to an array containing 2*@nchunks uint32_t
 *           the resulting data is [offset, size]
 * @nchunks : number of allocated chunks
 * return value : number of computed chunks
 */
int Orc::calc_pe_chunks_real(unsigned char* in, size_t inSize, PE_CHUNK* chunks, size_t nchunks)
{
    const size_t kChunkCount = 4;
    if (in == NULL || chunks == NULL || nchunks < kChunkCount)
    {
        return -1;
    }

    PE_IMAGE pe_img;
    if (parse_pe(in, inSize, &pe_img) < 0)
    {
        return -1;
    }

    /* XXX read security (certificates) directory */
    const PIMAGE_DATA_DIRECTORY secdir = pe_get_secdir(&pe_img);
    const size_t secdirOffset = reinterpret_cast<uint8_t*>(secdir) - in;

    chunks[0].offset = 0;
    chunks[0].length = pe_img.mPeCoffHeaderOffset + pe_get_checksum_offset(&pe_img);

    chunks[1].offset = chunks[0].length + sizeof(uint32_t);
    chunks[1].length = secdirOffset - chunks[1].offset;

    chunks[2].offset = secdirOffset + sizeof(IMAGE_DATA_DIRECTORY);
    chunks[2].length = pe_get_sizeof_headers(&pe_img) - chunks[2].offset;

    /* secdir (header) -> secdir (section) */
    chunks[3].offset = pe_get_sizeof_headers(&pe_img);
    chunks[3].length = inSize - chunks[3].offset - secdir->Size;

    // Chunks must not overlap themselves or overflow image size
    // All pe fields are at max 32 bits so it should be safe to use int64 to
    // avoid overflows.
    for (uint32_t i = 0; i < kChunkCount; ++i)
    {
        if (chunks[i].offset < 0 || chunks[i].length < 0)
        {
            return -1;
        }

        int64_t nextOffset;
        if (i != kChunkCount - 1)
        {
            nextOffset = chunks[i + 1].offset;
        }
        else
        {
            nextOffset = inSize;
        }

        if (chunks[i].offset + chunks[i].length > nextOffset)
        {
            return -1;
        }
    }

    return kChunkCount;
}
