//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <system_error>

#include <windows.h>

namespace Orc {

void RtlGetCompressionWorkSpaceSize(
    USHORT CompressionFormatAndEngine,
    PULONG CompressBufferWorkSpaceSize,
    PULONG CompressFragmentWorkSpaceSize,
    std::error_code& ec);

void RtlCompressBuffer(
    USHORT CompressionFormatAndEngine,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    ULONG UncompressedChunkSize,
    PULONG FinalCompressedSize,
    PVOID WorkSpace,
    std::error_code& ec);

void RtlDecompressBuffer(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PULONG FinalUncompressedSize,
    std::error_code& ec);

void RtlDecompressFragment(
    USHORT CompressionFormat,
    PUCHAR UncompressedFragment,
    ULONG UncompressedFragmentSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    ULONG FragmentOffset,
    PULONG FinalUncompressedSize,
    std::error_code& ec);

void RtlDecompressBufferEx(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PULONG FinalUncompressedSize,
    PVOID WorkSpace,
    std::error_code& ec);

void RtlDecompressFragmentEx(
    USHORT CompressionFormat,
    PUCHAR UncompressedFragment,
    ULONG UncompressedFragmentSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    ULONG FragmentOffset,
    ULONG UncompressedChunkSize,
    PULONG FinalUncompressedSize,
    PVOID WorkSpace,
    std::error_code& ec);

typedef struct _COMPRESSED_DATA_INFO
{
    WORD CompressionFormatAndEngine;
    UCHAR CompressionUnitShift;
    UCHAR ChunkShift;
    UCHAR ClusterShift;
    UCHAR Reserved;
    WORD NumberOfChunks;
    ULONG CompressedChunkSizes[1];
} COMPRESSED_DATA_INFO, *PCOMPRESSED_DATA_INFO;

void RtlDecompressChunks(
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PUCHAR CompressedTail,
    ULONG CompressedTailSize,
    PCOMPRESSED_DATA_INFO CompressedDataInfo,
    std::error_code& ec);

}  // namespace Orc
