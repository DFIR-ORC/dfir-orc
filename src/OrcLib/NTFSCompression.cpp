//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "NTFSCompression.h"

#include <spdlog/spdlog.h>

/* uncompression values */
constexpr auto NTFS_TOKEN_MASK = 1;
constexpr auto NTFS_SYMBOL_TOKEN = 0;
constexpr auto NTFS_TOKEN_LENGTH = 8;

constexpr auto NTFS_MAX_UNCOMPRESSION_BUFFER_SIZE = (64 * 1024);

using namespace Orc;

/**
 * Reset the values in the NTFS_COMP_INFO structure.  We need to
 * do this in between every compression unit that we process in the file.
 *
 * @param comp Structure to reset
 */
HRESULT Orc::ntfs_uncompress_reset(NTFS_COMP_INFO* comp)
{
    memset(comp->uncomp_buf, 0, comp->buf_size_b);
    comp->uncomp_idx = 0;
    memset(comp->comp_buf, 0, comp->buf_size_b);
    comp->comp_len = 0;
    return S_OK;
}

/**
 * Setup the NTFS_COMP_INFO structure with a buffer and
 * initialize the basic settings.
 *
 * @param fs File system state information
 * @param comp Compression state information to initialize
 * @param compunit_size_c The size (in clusters) of a compression
 * unit
 * @return 1 on error and 0 on success
 */
HRESULT
Orc::ntfs_uncompress_setup(unsigned int block_size, NTFS_COMP_INFO* comp, UINT compunit_size_c)
{
    if (!msl::utilities::SafeMultiply((size_t)block_size, compunit_size_c, comp->buf_size_b))
        return E_INVALIDARG;

    if ((comp->uncomp_buf = (char*)malloc(comp->buf_size_b)) == NULL)
    {
        comp->buf_size_b = 0;
        return E_OUTOFMEMORY;
    }
    if ((comp->comp_buf = (char*)malloc(comp->buf_size_b)) == NULL)
    {
        comp->buf_size_b = 0;
        return E_OUTOFMEMORY;
    }

    ntfs_uncompress_reset(comp);

    return S_OK;
}

HRESULT Orc::ntfs_uncompress_done(NTFS_COMP_INFO* comp)
{
    if (comp->uncomp_buf)
        free(comp->uncomp_buf);
    comp->uncomp_buf = NULL;
    if (comp->comp_buf)
        free(comp->comp_buf);
    comp->comp_buf = NULL;
    comp->buf_size_b = 0;
    return S_OK;
}

/**
 * Uncompress the block of data in comp->comp_buf,
 * which has a size of comp->comp_len.
 * Store the result in the comp->uncomp_buf.
 *
 * @param comp Compression unit structure
 *
 * @returns 1 on error and 0 on success
 */
HRESULT Orc::ntfs_uncompress_compunit(NTFS_COMP_INFO* comp)
{
    comp->uncomp_idx = 0;

    /* Cycle through the compressed data
     * We maintain state using different levels of loops.
     * We use +1 here because the size value at start of block is 2 bytes.
     */
    for (size_t cl_index = 0; cl_index + 1 < comp->comp_len;)
    {

        /* The first two bytes of each block contain the size
         * information.*/
        size_t blk_size =
            ((((unsigned char)comp->comp_buf[cl_index + 1] << 8) | ((unsigned char)comp->comp_buf[cl_index])) & 0x0FFF)
            + 3;

        // this seems to indicate end of block
        if (blk_size == 3)
            break;

        size_t blk_end = cl_index + blk_size;  // index into the buffer to where block ends
        if (blk_end > comp->comp_len)
        {
            spdlog::warn("ntfs_uncompress_compunit: Block length longer than buffer length: {}", blk_end);
            return E_FAIL;
        }

        spdlog::debug(L"ntfs_uncompress_compunit: Block size is %Iu", blk_size);

        /* The MSB identifies if the block is compressed */
        UCHAR iscomp;  // set to 1 if block is compressed
        if ((comp->comp_buf[cl_index + 1] & 0x8000) == 0)
            iscomp = 0;
        else
            iscomp = 1;

        // keep track of where this block started in the buffer
        size_t blk_st_uncomp = comp->uncomp_idx;  // index into uncompressed buffer where block started
        cl_index += 2;

        // the 4096 size seems to occur at the same times as no compression
        if ((iscomp) || (blk_size - 2 != 4096))
        {

            // cycle through the block
            while (cl_index < blk_end)
            {

                // get the header header
                unsigned char header = comp->comp_buf[cl_index];
                cl_index++;

                spdlog::debug("ntfs_uncompress_compunit: New Tag: {:#x}\n", header);

                for (int a = 0; a < 8 && cl_index < blk_end; a++)
                {

                    /* Determine token type and parse appropriately. *
                     * Symbol tokens are the symbol themselves, so copy it
                     * into the umcompressed buffer
                     */
                    if ((header & NTFS_TOKEN_MASK) == NTFS_SYMBOL_TOKEN)
                    {
                        spdlog::debug(L"ntfs_uncompress_compunit: Symbol Token: %Iu", cl_index);

                        if (comp->uncomp_idx >= comp->buf_size_b)
                        {
                            spdlog::warn(
                                L"ntfs_uncompress_compunit: Trying to write past end of uncompression buffer: {}",
                                comp->uncomp_idx);
                            return E_FAIL;
                        }
                        comp->uncomp_buf[comp->uncomp_idx++] = comp->comp_buf[cl_index];

                        cl_index++;
                    }

                    /* Otherwise, it is a phrase token, which points back
                     * to a previous sequence of bytes.
                     */
                    else
                    {

                        if (cl_index + 1 >= blk_end)
                        {
                            spdlog::warn(L"ntfs_uncompress_compunit: Phrase token index is past end of block: {}", a);
                            return E_FAIL;
                        }

                        uint16_t pheader =
                            ((((comp->comp_buf[cl_index + 1]) << 8) & 0xFF00) | (comp->comp_buf[cl_index] & 0xFF));
                        cl_index += 2;

                        /* The number of bits for the start and length
                         * in the 2-byte header change depending on the
                         * location in the compression unit.  This identifies
                         * how many bits each has */
                        int shift = 0;
                        for (size_t i = comp->uncomp_idx - blk_st_uncomp - 1; i >= 0x10; i >>= 1)
                        {
                            shift++;
                        }

                        unsigned int offset = (pheader >> (12 - shift)) + 1;
                        unsigned int length = (pheader & (0xFFF >> shift)) + 2;

                        size_t start_position_index = comp->uncomp_idx - offset;
                        size_t end_position_index = start_position_index + length;

                        spdlog::debug(
                            L"ntfs_uncompress_compunit: Phrase Token: %Iu\t%d\t%d\t%x",
                            cl_index,
                            length,
                            offset,
                            pheader);

                        /* Sanity checks on values */
                        if (offset > comp->uncomp_idx)
                        {
                            spdlog::warn(
                                L"ntfs_uncompress_compunit: Phrase token offset is too large: {} (max: {})",
                                offset,
                                comp->uncomp_idx);
                            return E_FAIL;
                        }
                        else if (length + start_position_index > comp->buf_size_b)
                        {
                            spdlog::warn(
                                L"ntfs_uncompress_compunit: Phrase token length is too large: {} (max: {})",
                                length,
                                comp->buf_size_b - start_position_index);
                            return E_FAIL;
                        }
                        else if (end_position_index - start_position_index + 1 > comp->buf_size_b - comp->uncomp_idx)
                        {
                            spdlog::warn(
                                L"ntfs_uncompress_compunit: Phrase token length is too large for rest of uncomp buf:  "
                                L"{} (max: {})",
                                end_position_index - start_position_index + 1,
                                comp->buf_size_b - comp->uncomp_idx);
                            return E_FAIL;
                        }

                        for (; start_position_index <= end_position_index && comp->uncomp_idx < comp->buf_size_b;
                             start_position_index++)
                        {

                            // Copy the previous data to the current position
                            comp->uncomp_buf[comp->uncomp_idx++] = comp->uncomp_buf[start_position_index];
                        }
                    }
                    header >>= 1;
                }  // end of loop inside of token group

            }  // end of loop inside of block
        }

        // this block contains uncompressed data uncompressed data
        else
        {
            while (cl_index < blk_end && cl_index < comp->comp_len)
            {
                /* This seems to happen only with corrupt data -- such as
                 * when an unallocated file is being processed... */
                if (comp->uncomp_idx >= comp->buf_size_b)
                {
                    spdlog::warn(
                        L"ntfs_uncompress_compunit: Trying to write past end of uncompression buffer (1) -- corrupt "
                        L"data?)");
                    return E_FAIL;
                }

                // Place data in uncompression_buffer
                comp->uncomp_buf[comp->uncomp_idx++] = comp->comp_buf[cl_index++];
            }
        }
    }  // end of loop inside of compression unit

    return S_OK;
}

#undef le16_to_cpup
/* the standard le16_to_cpup() crashes for unaligned data on some processors */
#define le16_to_cpup(p) (*(uint8_t*)(p) + (((uint8_t*)(p))[1] << 8))

/* Compression sub-block constants. */
constexpr auto NTFS_SB_SIZE_MASK = 0x0fff;
constexpr auto NTFS_SB_SIZE = 0x1000;
constexpr auto NTFS_SB_IS_COMPRESSED = 0x8000;

/**
 * ntfs_decompress - decompress a compression block into an array of pages
 * @dest:	buffer to which to write the decompressed data
 * @dest_size:	size of buffer @dest in bytes
 * @cb_start:	compression block to decompress
 * @cb_size:	size of compression block @cb_start in bytes
 *
 * This decompresses the compression block @cb_start into the destination
 * buffer @dest.
 *
 * @cb_start is a pointer to the compression block which needs decompressing
 * and @cb_size is the size of @cb_start in bytes (8-64kiB).
 *
 * Return 0 if success or -EOVERFLOW on error in the compressed stream.
 */
HRESULT Orc::ntfs_decompress(
    uint8_t* dest,
    const size_t dest_size,
    uint8_t* const cb_start,
    const size_t cb_size)
{
    /*
     * Pointers into the compressed data, i.e. the compression block (cb),
     * and the therein contained sub-blocks (sb).
     */
    uint8_t* cb_end = cb_start + cb_size; /* End of cb. */
    uint8_t* cb = cb_start; /* Current position in cb. */
    uint8_t* cb_sb_start = cb; /* Beginning of the current sb in the cb. */

    /* Variables for uncompressed data / destination. */
    uint8_t* dest_end = dest + dest_size; /* End of dest buffer. */

    /* Variables for tag and token parsing. */
    uint8_t tag; /* Current tag. */
    int token; /* Loop counter for the eight tokens in tag. */

    spdlog::debug(L"Entering, cb_size = {:#x}", cb_size);

do_next_sb:
    spdlog::debug(L"Beginning sub-block at offset {} in the cb", (int)(cb - cb_start));
    /*
     * Have we reached the end of the compression block or the end of the
     * decompressed data?  The latter can happen for example if the current
     * position in the compression block is one byte before its end so the
     * first two checks do not detect it.
     */
    if (cb == cb_end || !le16_to_cpup((uint16_t*)cb) || dest == dest_end)
    {
        spdlog::debug(L"Completed. Returning success (0)");
        return S_OK;
    }
    /* Setup offset for the current sub-block destination. */
    uint8_t* dest_sb_start = dest; /* Start of current sub-block in dest. */
    uint8_t* dest_sb_end = dest + NTFS_SB_SIZE; /* End of current sb in dest. */
    /* Check that we are still within allowed boundaries. */
    if (dest_sb_end > dest_end)
        goto return_overflow;
    /* Does the minimum size of a compressed sb overflow valid range? */
    if (cb + 6 > cb_end)
        goto return_overflow;
    /* Setup the current sub-block source pointers and validate range. */
    cb_sb_start = cb;
    uint8_t* cb_sb_end = cb_sb_start + (le16_to_cpup((uint16_t*)cb) & NTFS_SB_SIZE_MASK)
        + 3; /* End of current sb / beginning of next sb. */
    if (cb_sb_end > cb_end)
        goto return_overflow;
    /* Now, we are ready to process the current sub-block (sb). */
    if (!(le16_to_cpup((uint16_t*)cb) & NTFS_SB_IS_COMPRESSED))
    {
        spdlog::debug(L"Found uncompressed sub-block");
        /* This sb is not compressed, just copy it into destination. */
        /* Advance source position to first data byte. */
        cb += 2;
        /* An uncompressed sb must be full size. */
        if (cb_sb_end - cb != NTFS_SB_SIZE)
            goto return_overflow;
        /* Copy the block and advance the source position. */
        memcpy(dest, cb, NTFS_SB_SIZE);
        cb += NTFS_SB_SIZE;
        /* Advance destination position to next sub-block. */
        dest += NTFS_SB_SIZE;
        goto do_next_sb;
    }
    spdlog::debug(L"Found compressed sub-block");
    /* This sb is compressed, decompress it into destination. */
    /* Forward to the first tag in the sub-block. */
    cb += 2;
do_next_tag:
    if (cb == cb_sb_end)
    {
        /* Check if the decompressed sub-block was not full-length. */
        if (dest < dest_sb_end)
        {
            int nr_bytes = (int)(dest_sb_end - dest);

            spdlog::debug(L"Filling incomplete sub-block with zeroes");
            /* Zero remainder and update destination position. */
            memset(dest, 0, nr_bytes);
            dest += nr_bytes;
        }
        /* We have finished the current sub-block. */
        goto do_next_sb;
    }
    /* Check we are still in range. */
    if (cb > cb_sb_end || dest > dest_sb_end)
        goto return_overflow;
    /* Get the next tag and advance to first token. */
    tag = *cb++;
    /* Parse the eight tokens described by the tag. */
    for (token = 0; token < 8; token++, tag >>= 1)
    {
        uint16_t lg, pt, length, max_non_overlap;
        uint16_t i;
        uint8_t* dest_back_addr;

        /* Check if we are done / still in range. */
        if (cb >= cb_sb_end || dest > dest_sb_end)
            break;
        /* Determine token type and parse appropriately.*/
        if ((tag & NTFS_TOKEN_MASK) == NTFS_SYMBOL_TOKEN)
        {
            /*
             * We have a symbol token, copy the symbol across, and
             * advance the source and destination positions.
             */
            *dest++ = *cb++;
            /* Continue with the next token. */
            continue;
        }
        /*
         * We have a phrase token. Make sure it is not the first tag in
         * the sb as this is illegal and would confuse the code below.
         */
        if (dest == dest_sb_start)
            goto return_overflow;
        /*
         * Determine the number of bytes to go back (p) and the number
         * of bytes to copy (l). We use an optimized algorithm in which
         * we first calculate log2(current destination position in sb),
         * which allows determination of l and p in O(1) rather than
         * O(n). We just need an arch-optimized log2() function now.
         */
        lg = 0;
        for (i = (uint16_t)(dest - dest_sb_start - 1); i >= 0x10; i >>= 1)
            lg++;
        /* Get the phrase token into i. */
        pt = le16_to_cpup((uint16_t*)cb);
        /*
         * Calculate starting position of the byte sequence in
         * the destination using the fact that p = (pt >> (12 - lg)) + 1
         * and make sure we don't go too far back.
         */
        dest_back_addr = dest - (pt >> (12 - lg)) - 1;
        if (dest_back_addr < dest_sb_start)
            goto return_overflow;
        /* Now calculate the length of the byte sequence. */
        length = (pt & (0xfff >> lg)) + 3;
        /* Verify destination is in range. */
        if (dest + length > dest_sb_end)
            goto return_overflow;
        /* The number of non-overlapping bytes. */
        max_non_overlap = (uint16_t)(dest - dest_back_addr);
        if (length <= max_non_overlap)
        {
            /* The byte sequence doesn't overlap, just copy it. */
            memcpy(dest, dest_back_addr, length);
            /* Advance destination pointer. */
            dest += length;
        }
        else
        {
            /*
             * The byte sequence does overlap, copy non-overlapping
             * part and then do a slow byte by byte copy for the
             * overlapping part. Also, advance the destination
             * pointer.
             */
            memcpy(dest, dest_back_addr, max_non_overlap);
            dest += max_non_overlap;
            dest_back_addr += max_non_overlap;
            length -= max_non_overlap;
            while (length--)
                *dest++ = *dest_back_addr++;
        }
        /* Advance source position and continue with the next token. */
        cb += 2;
    }
    /* No tokens left in the current tag. Continue with the next tag. */
    goto do_next_tag;
return_overflow:
    spdlog::error(L"Failed to decompress file");
    return E_FAIL;
}
