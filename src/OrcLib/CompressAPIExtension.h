//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#pragma managed(push, off)

namespace Orc {

using COMPRESSOR_HANDLE = HANDLE;

using PCOMPRESSOR_HANDLE = COMPRESSOR_HANDLE*;
using DECOMPRESSOR_HANDLE = COMPRESSOR_HANDLE;
using PDECOMPRESSOR_HANDLE = COMPRESSOR_HANDLE*;

using PFN_COMPRESS_ALLOCATE = PVOID(__cdecl*)(_In_ PVOID UserContext, _In_ SIZE_T Size);
using PFN_COMPRESS_FREE = VOID(__cdecl*)(_In_ PVOID UserContext, _In_ PVOID Memory);

struct COMPRESS_ALLOCATION_ROUTINES
{
    PFN_COMPRESS_ALLOCATE Allocate;
    PFN_COMPRESS_FREE Free;
    PVOID UserContext;
};
using PCOMPRESS_ALLOCATION_ROUTINES = COMPRESS_ALLOCATION_ROUTINES*;

enum COMPRESS_INFORMATION_CLASS
{
    COMPRESS_INFORMATION_CLASS_INVALID = 0,
    COMPRESS_INFORMATION_CLASS_BLOCK_SIZE,
    COMPRESS_INFORMATION_CLASS_LEVEL
};

static constexpr auto COMPRESS_ALGORITHM_INVALID = 0;
static constexpr auto COMPRESS_ALGORITHM_NULL = 1;
static constexpr auto COMPRESS_ALGORITHM_MSZIP = 2;
static constexpr auto COMPRESS_ALGORITHM_XPRESS = 3;
static constexpr auto COMPRESS_ALGORITHM_XPRESS_HUFF = 4;
static constexpr auto COMPRESS_ALGORITHM_LZMS = 5;
static constexpr auto COMPRESS_ALGORITHM_MAX = 6;

static constexpr auto COMPRESS_RAW = (1 << 29);

class CompressAPIExtension : public ExtensionLibrary
{
public:
    CompressAPIExtension()
        : ExtensionLibrary(L"cabinet", L"cabinet.dll", L"cabinet.dll") {};

    STDMETHOD(Initialize)();

    template <typename... Args>
    auto CreateCompressor(Args&&... args)
    {
        return Win32Call(m_CreateCompressor, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto SetCompressorInformation(Args&&... args)
    {
        return Win32Call(m_SetCompressorInformation, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto QueryCompressorInformation(Args&&... args)
    {
        return Win32Call(m_QueryCompressorInformation, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto Compress(Args&&... args)
    {
        return Win32Call(m_Compress, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto ResetCompressor(Args&&... args)
    {
        return Win32Call(m_ResetCompressor, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto CloseCompressor(Args&&... args)
    {
        return Win32Call(m_CloseCompressor, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto CreateDecompressor(Args&&... args)
    {
        return Win32Call(m_CreateDecompressor, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto SetDecompressorInformation(Args&&... args)
    {
        return Win32Call(m_SetDecompressorInformation, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto QueryDecompressorInformation(Args&&... args)
    {
        return Win32Call(m_QueryDecompressorInformation, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto Decompress(Args&&... args)
    {
        return Win32Call(m_Decompress, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto ResetDecompressor(Args&&... args)
    {
        return Win32Call(m_ResetDecompressor, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto CloseDecompressor(Args&&... args)
    {
        return Win32Call(m_CloseDecompressor, std::forward<Args>(args)...);
    }

private:
    // Compression routines
    BOOL(WINAPI* m_CreateCompressor)
    (DWORD Algorithm, PCOMPRESS_ALLOCATION_ROUTINES AllocationRoutines, PCOMPRESSOR_HANDLE CompressorHandle);
    BOOL(WINAPI* m_SetCompressorInformation)
    (_In_ COMPRESSOR_HANDLE CompressorHandle,
     _In_ COMPRESS_INFORMATION_CLASS CompressInformationClass,
     _In_reads_bytes_(CompressInformationSize) LPCVOID CompressInformation,
     _In_ SIZE_T CompressInformationSize);
    BOOL(WINAPI* m_QueryCompressorInformation)
    (_In_ COMPRESSOR_HANDLE CompressorHandle,
     _In_ COMPRESS_INFORMATION_CLASS CompressInformationClass,
     _Out_writes_bytes_(CompressInformationSize) PVOID CompressInformation,
     _In_ SIZE_T CompressInformationSize);
    BOOL(WINAPI* m_Compress)
    (_In_ COMPRESSOR_HANDLE CompressorHandle,
     _In_reads_bytes_opt_(UncompressedDataSize) LPCVOID UncompressedData,
     _In_ SIZE_T UncompressedDataSize,
     _Out_writes_bytes_opt_(CompressedBufferSize) PVOID CompressedBuffer,
     _In_ SIZE_T CompressedBufferSize,
     _Out_ PSIZE_T CompressedDataSize);
    BOOL(WINAPI* m_ResetCompressor)(_In_ COMPRESSOR_HANDLE CompressorHandle);
    BOOL(WINAPI* m_CloseCompressor)(_In_ COMPRESSOR_HANDLE CompressorHandle);

    // Decompression routines
    BOOL(WINAPI* m_CreateDecompressor)
    (_In_ DWORD Algorithm,
     _In_opt_ PCOMPRESS_ALLOCATION_ROUTINES AllocationRoutines,
     _Out_ PDECOMPRESSOR_HANDLE DecompressorHandle);
    BOOL(WINAPI* m_SetDecompressorInformation)
    (_In_ DECOMPRESSOR_HANDLE DecompressorHandle,
     _In_ COMPRESS_INFORMATION_CLASS CompressInformationClass,
     _In_reads_bytes_(CompressInformationSize) LPCVOID CompressInformation,
     _In_ SIZE_T CompressInformationSize);
    BOOL(WINAPI* m_QueryDecompressorInformation)
    (_In_ DECOMPRESSOR_HANDLE DecompressorHandle,
     _In_ COMPRESS_INFORMATION_CLASS CompressInformationClass,
     _Out_writes_bytes_(CompressInformationSize) PVOID CompressInformation,
     _In_ SIZE_T CompressInformationSize);
    BOOL(WINAPI* m_Decompress)
    (_In_ DECOMPRESSOR_HANDLE DecompressorHandle,
     _In_reads_bytes_opt_(CompressedDataSize) LPCVOID CompressedData,
     _In_ SIZE_T CompressedDataSize,
     _Out_writes_bytes_opt_(UncompressedBufferSize) PVOID UncompressedBuffer,
     _In_ SIZE_T UncompressedBufferSize,
     _Out_opt_ PSIZE_T UncompressedDataSize);
    BOOL(WINAPI* m_ResetDecompressor)(_In_ DECOMPRESSOR_HANDLE DecompressorHandle);
    BOOL(WINAPI* m_CloseDecompressor)(_In_ DECOMPRESSOR_HANDLE DecompressorHandle);
};

}  // namespace Orc

#pragma managed(pop)
