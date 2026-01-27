//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Compression.h"

#include <array>
#include <memory>

#include <zstd.h>

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace {

constexpr std::array<uint8_t, 6> k7zSignature = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};

struct ZstdCCtxDeleter
{
    void operator()(ZSTD_CCtx* cctx) const noexcept { ZSTD_freeCCtx(cctx); }
};
using ScopedCCtx = std::unique_ptr<ZSTD_CCtx, ZstdCCtxDeleter>;

struct ZstdDCtxDeleter
{
    void operator()(ZSTD_DCtx* dctx) const noexcept { ZSTD_freeDCtx(dctx); }
};
using ScopedDCtx = std::unique_ptr<ZSTD_DCtx, ZstdDCtxDeleter>;

[[nodiscard]] std::error_code SetZstdParam(ZSTD_CCtx* cctx, ZSTD_cParameter param, int value, std::string_view name)
{
    const size_t result = ZSTD_CCtx_setParameter(cctx, param, value);
    if (ZSTD_isError(result))
    {
        Log::Debug("Failed ZSTD_CCtx_setParameter for {} [{}]", name, ZSTD_getErrorName(result));
        return std::make_error_code(std::errc::bad_message);
    }

    return {};
}

[[nodiscard]] std::error_code ConfigurePeCompressionParams(ZSTD_CCtx* cctx)
{
#ifdef _DEBUG
    const int level = 5;
    const int windowLog = 0;  // automatic
    const int strategy = ZSTD_fast;
    // const int nbWorkers = 0;
#else
    const int level = ZSTD_maxCLevel();
    const int windowLog = 23;  // 23: 8 MB
    const int strategy = ZSTD_btultra2;
    // const int nbWorkers = static_cast<int>(std::thread::hardware_concurrency());
#endif

    const int nbWorkers = 0;

    // clang-format off
    if (auto ec = SetZstdParam(cctx, ZSTD_c_compressionLevel, level, "compressionLevel")) return ec;
    if (auto ec = SetZstdParam(cctx, ZSTD_c_windowLog, windowLog, "windowLog")) return ec;
    if (auto ec = SetZstdParam(cctx, ZSTD_c_strategy, strategy,  "strategy")) return ec;
    if (auto ec = SetZstdParam(cctx, ZSTD_c_enableLongDistanceMatching, 1, "ldm")) return ec;
    if (auto ec = SetZstdParam(cctx, ZSTD_c_ldmMinMatch, 64, "ldmMinMatch")) return ec;  // PE sections repeat at >64B
    if (auto ec = SetZstdParam(cctx, ZSTD_c_checksumFlag, 1, "checksum")) return ec;
    if (auto ec = SetZstdParam(cctx, ZSTD_c_nbWorkers, nbWorkers, "nbWorkers")) return ec;
    // clang-format on

    return {};
}

}  // namespace

[[nodiscard]] bool Is7zArchive(std::basic_string_view<uint8_t> data)
{
    return std::equal(k7zSignature.begin(), k7zSignature.end(), data.begin());
}

[[nodiscard]] bool IsZstdArchive(std::basic_string_view<uint8_t> buffer)
{
    static constexpr std::array<uint8_t, 4> ZSTD_MAGIC = {0x28, 0xB5, 0x2F, 0xFD};

    if (buffer.size() < ZSTD_MAGIC.size())
    {
        return false;
    }

    return std::equal(ZSTD_MAGIC.begin(), ZSTD_MAGIC.end(), buffer.begin());
}

[[nodiscard]] std::error_code ZstdCompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    std::error_code ec;

    if (input.empty())
    {
        output.clear();
        return {};
    }

    ScopedCCtx cctx(ZSTD_createCCtx());
    if (!cctx)
    {
        Log::Debug("Failed ZSTD_createCCtx");
        return std::make_error_code(std::errc::bad_message);
    }

    if (auto ec = ConfigurePeCompressionParams(cctx.get()))
    {
        Log::Debug(L"Failed to configure compression [{}]", ec);
        return ec;
    }

    size_t bound = ZSTD_compressBound(input.size());
    if (bound == 0)
    {
        Log::Debug("Failed ZSTD_compressBound (input size: {}, output size: {})", input.size(), bound);
        return std::make_error_code(std::errc::bad_message);
    }

    try
    {
        output.resize(bound);
    }
    catch (const std::bad_alloc& e)
    {
        Log::Critical("Failed to allocate {} bytes for compression: {}", bound, e.what());
        return std::make_error_code(std::errc::not_enough_memory);
    }

    // Perform compression with using parallel ZSTD_compress2
    size_t compressedSize = ZSTD_compress2(cctx.get(), output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(compressedSize))
    {
        Log::Debug("Failed ZSTD_compress2 [{}]", ZSTD_getErrorName(compressedSize));
        return std::make_error_code(std::errc::bad_message);
    }

    if (compressedSize > output.size())
    {
        Log::Warn("Compressed size {} exceeds buffer size {}", compressedSize, output.size());
        output = input;
        return {};
    }

    output.resize(compressedSize);

    return {};
}

[[nodiscard]] std::error_code ZstdDecompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    if (input.empty())
    {
        output.clear();
        return {};
    }

    unsigned long long uncompressedSize = ZSTD_getFrameContentSize(input.data(), input.size());
    if (uncompressedSize == ZSTD_CONTENTSIZE_ERROR)
    {
        Log::Debug(L"Failed ZSTD_getFrameContentSize: ZSTD_CONTENTSIZE_ERROR");
        return std::make_error_code(std::errc::bad_message);
    }

    if (uncompressedSize == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        Log::Debug(L"Failed ZSTD_getFrameContentSize: ZSTD_CONTENTSIZE_UNKNOWN");
        return std::make_error_code(std::errc::bad_message);
    }

    ScopedDCtx dctx(ZSTD_createDCtx());
    if (!dctx)
    {
        Log::Debug(L"Failed ZSTD_createDCtx");
        return std::make_error_code(std::errc::bad_message);
    }

    std::vector<uint8_t> decompressed;
    try
    {
        decompressed.resize(uncompressedSize);
    }
    catch (const std::bad_alloc& e)
    {
        Log::Critical("Failed to allocate {} bytes for decompression: {}", uncompressedSize, e.what());
        return std::make_error_code(std::errc::not_enough_memory);
    }

    const size_t actualSize =
        ZSTD_decompressDCtx(dctx.get(), decompressed.data(), decompressed.size(), input.data(), input.size());

    if (ZSTD_isError(actualSize))
    {
        Log::Error("Failed ZSTD_decompressDCtx [{}]", std::string(ZSTD_getErrorName(actualSize)));
        return std::make_error_code(std::errc::bad_message);
    }

    output = std::move(decompressed);
    return {};
}

std::error_code ZstdGetCompressRatio(std::basic_string_view<uint8_t> buffer, uint64_t& compressedSize, double& ratio)
{
    const unsigned long long frameSize = ZSTD_getFrameContentSize(buffer.data(), buffer.size());

    if (frameSize == ZSTD_CONTENTSIZE_UNKNOWN)
        return std::make_error_code(std::errc::not_supported);

    if (frameSize == ZSTD_CONTENTSIZE_ERROR || frameSize == 0)
        return std::make_error_code(std::errc::invalid_argument);

    compressedSize = static_cast<uint64_t>(frameSize);
    ratio = (static_cast<double>(buffer.size()) / static_cast<double>(compressedSize)) * 100.0;
    return {};
}
