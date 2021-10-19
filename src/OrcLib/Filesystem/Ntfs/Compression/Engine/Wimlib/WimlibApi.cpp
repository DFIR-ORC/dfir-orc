//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WimlibApi.h"

#include <system_error>

#include <windows.h>

#include "Filesystem/Ntfs/Compression/Engine/wimlib/WimlibErrorCategory.h"
#include "Utils/Guard.h"

namespace {

Orc::Guard::Module& LoadWimlib(std::error_code& ec)
{
    using namespace Orc::Guard;
    static Module dll;

    if (!dll)
    {
        Module module(::LoadLibraryW(L"libwim-15.dll"));
        if (!module)
        {
            ec.assign(::GetLastError(), std::system_category());
            return dll;
        }

        dll = std::move(module);
    }

    return dll;
};

}  // namespace

namespace Orc {

void wimlib_set_default_compression_level(int ctype, unsigned int compression_level, std::error_code& ec)
{
    using fn_wimlib_set_default_compression_level = int(__stdcall*)(int ctype, unsigned int compression_level);

    static fn_wimlib_set_default_compression_level fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<fn_wimlib_set_default_compression_level>(
            ::GetProcAddress(*dll, "wimlib_set_default_compression_level"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    const auto status = fn(ctype, compression_level);
    if (status != 0)
    {
        ec.assign(status, wimlib_error_category());
    }
}

uint64_t wimlib_get_compressor_needed_memory(
    enum wimlib_compression_type ctype,
    size_t max_block_size,
    unsigned int compression_level,
    std::error_code& ec)
{
    using fn_wimlib_get_compressor_needed_memory =
        uint64_t(__stdcall*)(enum wimlib_compression_type ctype, size_t max_block_size, unsigned int compression_level);

    static fn_wimlib_get_compressor_needed_memory fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return 0;
        }

        fn = reinterpret_cast<fn_wimlib_get_compressor_needed_memory>(
            ::GetProcAddress(*dll, "wimlib_get_compressor_needed_memory"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return 0;
        }
    }

    const auto status = fn(ctype, max_block_size, compression_level);
    if (status == 0)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }

    return status;
}

void wimlib_create_compressor(
    enum wimlib_compression_type ctype,
    size_t max_block_size,
    unsigned int compression_level,
    struct wimlib_compressor** compressor_ret,
    std::error_code& ec)
{
    using fn_wimlib_create_compressor = int(__stdcall*)(
        enum wimlib_compression_type ctype,
        size_t max_block_size,
        unsigned int compression_level,
        struct wimlib_compressor** compressor_ret);

    static fn_wimlib_create_compressor fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<fn_wimlib_create_compressor>(::GetProcAddress(*dll, "wimlib_create_compressor"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    const auto status = fn(ctype, max_block_size, compression_level, compressor_ret);
    if (status != 0)
    {
        ec.assign(status, wimlib_error_category());
    }
}

size_t wimlib_compress(
    const void* uncompressed_data,
    size_t uncompressed_size,
    void* compressed_data,
    size_t compressed_size_avail,
    struct wimlib_compressor* compressor,
    std::error_code& ec)
{
    using fn_wimlib_compress = size_t(__stdcall*)(
        const void* uncompressed_data,
        size_t uncompressed_size,
        void* compressed_data,
        size_t compressed_size_avail,
        struct wimlib_compressor* compressor);

    static fn_wimlib_compress fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return 0;
        }

        fn = reinterpret_cast<fn_wimlib_compress>(::GetProcAddress(*dll, "wimlib_compress"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return 0;
        }
    }

    const auto status = fn(uncompressed_data, uncompressed_size, compressed_data, compressed_size_avail, compressor);

    if (status == 0)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }

    return status;
}

void wimlib_free_compressor(struct wimlib_compressor* compressor, std::error_code& ec)
{
    using fn_wimlib_free_compressor = void(__stdcall*)(struct wimlib_compressor * compressor);

    static fn_wimlib_free_compressor fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<fn_wimlib_free_compressor>(::GetProcAddress(*dll, "wimlib_free_compressor"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    fn(compressor);
}

void wimlib_create_decompressor(
    enum wimlib_compression_type ctype,
    size_t max_block_size,
    struct wimlib_decompressor** decompressor_ret,
    std::error_code& ec)
{
    using fn_wimlib_create_decompressor = int(__stdcall*)(
        enum wimlib_compression_type ctype, size_t max_block_size, struct wimlib_decompressor * *decompressor_ret);

    static fn_wimlib_create_decompressor fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<fn_wimlib_create_decompressor>(::GetProcAddress(*dll, "wimlib_create_decompressor"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    const auto status = fn(ctype, max_block_size, decompressor_ret);
    if (status != 0)
    {
        ec.assign(status, wimlib_error_category());
    }
}

void wimlib_decompress(
    const void* compressed_data,
    size_t compressed_size,
    void* uncompressed_data,
    size_t uncompressed_size,
    struct wimlib_decompressor* decompressor,
    std::error_code& ec)
{
    using fn_wimlib_decompress = int(__stdcall*)(
        const void* compressed_data,
        size_t compressed_size,
        void* uncompressed_data,
        size_t uncompressed_size,
        struct wimlib_decompressor* decompressor);

    static fn_wimlib_decompress fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<fn_wimlib_decompress>(::GetProcAddress(*dll, "wimlib_decompress"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    const auto status = fn(compressed_data, compressed_size, uncompressed_data, uncompressed_size, decompressor);
    if (status != 0)
    {
        ec = std::make_error_code(std::errc::invalid_argument);
    }
}

void wimlib_free_decompressor(struct wimlib_decompressor* decompressor, std::error_code& ec)
{
    using fn_wimlib_free_decompressor = void(__stdcall*)(struct wimlib_decompressor * compressor);

    static fn_wimlib_free_decompressor fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return;
        }

        fn = reinterpret_cast<fn_wimlib_free_decompressor>(::GetProcAddress(*dll, "wimlib_free_decompressor"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return;
        }
    }

    fn(decompressor);
}

const wimlib_tchar* wimlib_get_error_string(wimlib_errc code, std::error_code& ec)
{
    using fn_wimlib_get_error_string = const wimlib_tchar*(__stdcall*)(int);

    static fn_wimlib_get_error_string fn = nullptr;

    if (fn == nullptr)
    {
        const auto& dll = ::LoadWimlib(ec);
        if (ec)
        {
            return {};
        }

        fn = reinterpret_cast<fn_wimlib_get_error_string>(::GetProcAddress(*dll, "wimlib_get_error_string"));

        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return {};
        }
    }

    return fn(std::underlying_type_t<wimlib_errc>(code));
}

}  // namespace Orc
