//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Crypto.h"

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"

#include "fmt/std_error_code.h"
#include "fmt/std_filesystem.h"

namespace {

struct SHA256Context
{
    uint32_t state[8];
    uint64_t totalBits;
    uint8_t buffer[64];
    uint32_t bufferLen;
};

inline uint32_t RoR(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

void Transform(uint32_t state[8], const uint8_t data[64])
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (uint32_t)data[i * 4] << 24 | (uint32_t)data[i * 4 + 1] << 16 | (uint32_t)data[i * 4 + 2] << 8
            | (uint32_t)data[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i)
    {
        uint32_t s0 = RoR(w[i - 15], 7) ^ RoR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = RoR(w[i - 2], 17) ^ RoR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];

    for (int i = 0; i < 64; ++i)
    {
        uint32_t S1 = RoR(e, 6) ^ RoR(e, 11) ^ RoR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = RoR(a, 2) ^ RoR(a, 13) ^ RoR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void Update(SHA256Context& ctx, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        ctx.buffer[ctx.bufferLen++] = data[i];
        if (ctx.bufferLen == 64)
        {
            Transform(ctx.state, ctx.buffer);
            ctx.totalBits += 512;
            ctx.bufferLen = 0;
        }
    }
}

void Final(SHA256Context& ctx, uint8_t hash[32])
{
    uint64_t totalBits = ctx.totalBits + (ctx.bufferLen * 8);
    ctx.buffer[ctx.bufferLen++] = 0x80;

    if (ctx.bufferLen > 56)
    {
        while (ctx.bufferLen < 64)
            ctx.buffer[ctx.bufferLen++] = 0;
        Transform(ctx.state, ctx.buffer);
        ctx.bufferLen = 0;
    }
    while (ctx.bufferLen < 56)
        ctx.buffer[ctx.bufferLen++] = 0;

    // Append total bits as Big-Endian 64-bit int
    for (int i = 0; i < 8; ++i)
        ctx.buffer[56 + i] = (uint8_t)(totalBits >> (56 - i * 8));
    Transform(ctx.state, ctx.buffer);

    for (int i = 0; i < 8; ++i)
    {
        hash[i * 4] = (uint8_t)(ctx.state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx.state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx.state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx.state[i]);
    }
}

std::wstring BytesToHex(const std::vector<uint8_t>& buffer)
{
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring res;
    res.resize(buffer.size() * 2);
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        uint8_t b = buffer[i];
        res[i * 2] = hex[b >> 4];
        res[i * 2 + 1] = hex[b & 0x0F];
    }

    return res;
}

}  // namespace

std::error_code ComputeSha256(const std::filesystem::path& path, std::vector<uint8_t>& sha256)
{
    ScopedFileHandle hFile(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (hFile.get() == INVALID_HANDLE_VALUE)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed CreateFileW (path: {}) [{}]", path, ec);
        return ec;
    }

    ::SHA256Context ctx = {
        {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, 0, {0}, 0};

    constexpr DWORD kBufferSize = 64 * 1024;
    std::vector<uint8_t> buffer(kBufferSize);
    DWORD bytesRead = 0;

    while (true)
    {
        bytesRead = 0;

        if (!ReadFile(hFile.get(), buffer.data(), kBufferSize, &bytesRead, nullptr))
        {
            auto ec = LastWin32Error();
            Log::Debug(L"Failed ReadFile (path: {}) [{}]", path, ec);
            return ec;
        }

        if (bytesRead == 0)
        {
            break;
        }

        ::Update(ctx, buffer.data(), bytesRead);
    }

    sha256.resize(32);
    ::Final(ctx, sha256.data());
    return {};
}

std::error_code ComputeSha256(const std::filesystem::path& path, std::wstring& sha256)
{
    std::vector<uint8_t> rawHash;
    auto ec = ComputeSha256(path, rawHash);
    if (ec)
    {
        return ec;
    }

    sha256 = BytesToHex(rawHash);
    return {};
}

std::error_code ComputeSha256(const std::vector<uint8_t>& data, std::vector<uint8_t>& sha256)
{
    ::SHA256Context ctx = {
        {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, 0, {0}, 0};

    ::Update(ctx, data.data(), data.size());

    sha256.resize(32);
    ::Final(ctx, sha256.data());
    return {};
}

std::error_code ComputeSha256(const std::vector<uint8_t>& data, std::wstring& sha256)
{
    std::vector<uint8_t> rawHash;
    auto ec = ComputeSha256(data, rawHash);
    if (ec)
    {
        return ec;
    }

    sha256 = BytesToHex(rawHash);
    return {};
}
