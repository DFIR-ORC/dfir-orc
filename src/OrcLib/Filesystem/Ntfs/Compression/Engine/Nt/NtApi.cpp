//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "NtApi.h"

#include <string>
#include <system_error>

#include <windows.h>

#include "Utils/Guard.h"

using Win32Error = ULONG;

namespace {

HMODULE LoadNtdll(std::error_code& ec)
{
    static HMODULE module = NULL;

    if (!module)
    {
        module = GetModuleHandleW(L"ntdll.dll");
        if (module == NULL)
        {
            ec.assign(::GetLastError(), std::system_category());
            return NULL;
        }
    }

    return module;
};

}  // namespace

namespace Orc {

void RtlGetCompressionWorkSpaceSize(
    USHORT CompressionFormatAndEngine,
    PULONG CompressBufferWorkSpaceSize,
    PULONG CompressFragmentWorkSpaceSize,
    std::error_code& ec)
{
    using FnRtlGetCompressionWorkSpaceSize = LONG(__stdcall*)(
        USHORT CompressionFormatAndEngine, PULONG CompressBufferWorkSpaceSize, PULONG CompressFragmentWorkSpaceSize);

    static FnRtlGetCompressionWorkSpaceSize fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn =
            reinterpret_cast<FnRtlGetCompressionWorkSpaceSize>(::GetProcAddress(dll, "RtlGetCompressionWorkSpaceSize"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status = fn(CompressionFormatAndEngine, CompressBufferWorkSpaceSize, CompressFragmentWorkSpaceSize);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

void RtlCompressBuffer(
    USHORT CompressionFormatAndEngine,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    ULONG UncompressedChunkSize,
    PULONG FinalCompressedSize,
    PVOID WorkSpace,
    std::error_code& ec)
{
    using FnRtlCompressBuffer = LONG(__stdcall*)(
        USHORT CompressionFormatAndEngine,
        PUCHAR UncompressedBuffer,
        ULONG UncompressedBufferSize,
        PUCHAR CompressedBuffer,
        ULONG CompressedBufferSize,
        ULONG UncompressedChunkSize,
        PULONG FinalCompressedSize,
        PVOID WorkSpace);

    static FnRtlCompressBuffer fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<FnRtlCompressBuffer>(::GetProcAddress(dll, "RtlCompressBuffer"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status =
        fn(CompressionFormatAndEngine,
           UncompressedBuffer,
           UncompressedBufferSize,
           CompressedBuffer,
           CompressedBufferSize,
           UncompressedChunkSize,
           FinalCompressedSize,
           WorkSpace);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

void RtlDecompressBuffer(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PULONG FinalUncompressedSize,
    std::error_code& ec)
{
    using FnRtlDecompressBuffer = LONG(__stdcall*)(
        USHORT CompressionFormat,
        PUCHAR UncompressedBuffer,
        ULONG UncompressedBufferSize,
        PUCHAR CompressedBuffer,
        ULONG CompressedBufferSize,
        PULONG FinalUncompressedSize);

    static FnRtlDecompressBuffer fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<FnRtlDecompressBuffer>(::GetProcAddress(dll, "RtlDecompressBuffer"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status =
        fn(CompressionFormat,
           UncompressedBuffer,
           UncompressedBufferSize,
           CompressedBuffer,
           CompressedBufferSize,
           FinalUncompressedSize);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

void RtlDecompressFragment(
    USHORT CompressionFormat,
    PUCHAR UncompressedFragment,
    ULONG UncompressedFragmentSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    ULONG FragmentOffset,
    PULONG FinalUncompressedSize,
    std::error_code& ec)
{
    using FnRtlDecompressFragment = LONG(__stdcall*)(
        USHORT CompressionFormat,
        PUCHAR UncompressedFragment,
        ULONG UncompressedFragmentSize,
        PUCHAR CompressedBuffer,
        ULONG CompressedBufferSize,
        ULONG FragmentOffset,
        PULONG FinalUncompressedSize);

    static FnRtlDecompressFragment fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<FnRtlDecompressFragment>(::GetProcAddress(dll, "RtlDecompressFragment"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status =
        fn(CompressionFormat,
           UncompressedFragment,
           UncompressedFragmentSize,
           CompressedBuffer,
           CompressedBufferSize,
           FragmentOffset,
           FinalUncompressedSize);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

void RtlDecompressBufferEx(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PULONG FinalUncompressedSize,
    PVOID WorkSpace,
    std::error_code& ec)
{
    using FnRtlDecompressBufferEx = LONG(__stdcall*)(
        USHORT CompressionFormat,
        PUCHAR UncompressedBuffer,
        ULONG UncompressedBufferSize,
        PUCHAR CompressedBuffer,
        ULONG CompressedBufferSize,
        PULONG FinalUncompressedSize,
        PVOID WorkSpace);

    static FnRtlDecompressBufferEx fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<FnRtlDecompressBufferEx>(::GetProcAddress(dll, "RtlDecompressBufferEx"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status =
        fn(CompressionFormat,
           UncompressedBuffer,
           UncompressedBufferSize,
           CompressedBuffer,
           CompressedBufferSize,
           FinalUncompressedSize,
           WorkSpace);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

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
    std::error_code& ec)
{
    using FnRtlDecompressFragmentEx = LONG(__stdcall*)(
        USHORT CompressionFormat,
        PUCHAR UncompressedFragment,
        ULONG UncompressedFragmentSize,
        PUCHAR CompressedBuffer,
        ULONG CompressedBufferSize,
        ULONG FragmentOffset,
        ULONG UncompressedChunkSize,
        PULONG FinalUncompressedSize,
        PVOID WorkSpace);

    static FnRtlDecompressFragmentEx fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<FnRtlDecompressFragmentEx>(::GetProcAddress(dll, "RtlDecompressFragmentEx"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status =
        fn(CompressionFormat,
           UncompressedFragment,
           UncompressedFragmentSize,
           CompressedBuffer,
           CompressedBufferSize,
           FragmentOffset,
           UncompressedChunkSize,
           FinalUncompressedSize,
           WorkSpace);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

void RtlDecompressChunks(
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PUCHAR CompressedTail,
    ULONG CompressedTailSize,
    PCOMPRESSED_DATA_INFO CompressedDataInfo,
    std::error_code& ec)
{
    using FnRtlDecompressChunks = LONG(__stdcall*)(
        PUCHAR UncompressedBuffer,
        ULONG UncompressedBufferSize,
        PUCHAR CompressedBuffer,
        ULONG CompressedBufferSize,
        PUCHAR CompressedTail,
        ULONG CompressedTailSize,
        PCOMPRESSED_DATA_INFO CompressedDataInfo);

    static FnRtlDecompressChunks fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadNtdll(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<FnRtlDecompressChunks>(::GetProcAddress(dll, "RtlDecompressChunks"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    auto status =
        fn(UncompressedBuffer,
           UncompressedBufferSize,
           CompressedBuffer,
           CompressedBufferSize,
           CompressedTail,
           CompressedTailSize,
           CompressedDataInfo);
    if (status != 0)
    {
        ec.assign(status, std::system_category());
        return;
    }
}

}  // namespace Orc
