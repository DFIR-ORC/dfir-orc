//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#include "stdafx.h"

#include "HmacSha256.h"

#include "BCryptApi.h"
#include "Log/Log.h"

#include <climits>

namespace {

// Closes an algorithm provider handle on every exit path.
class AlgorithmGuard final
{
public:
    explicit AlgorithmGuard(BCRYPT_ALG_HANDLE handle)
        : m_handle(handle)
    {
    }

    ~AlgorithmGuard()
    {
        if (m_handle != NULL)
        {
            std::error_code ec;
            Orc::BCryptApi::CloseAlgorithmProvider(m_handle, ec);
        }
    }

    AlgorithmGuard(const AlgorithmGuard&) = delete;
    AlgorithmGuard& operator=(const AlgorithmGuard&) = delete;

private:
    BCRYPT_ALG_HANDLE m_handle;
};

// Destroys a hash handle on every exit path.
class HashGuard final
{
public:
    explicit HashGuard(BCRYPT_HASH_HANDLE handle)
        : m_handle(handle)
    {
    }

    ~HashGuard()
    {
        if (m_handle != NULL)
        {
            std::error_code ec;
            Orc::BCryptApi::DestroyHash(m_handle, ec);
        }
    }

    HashGuard(const HashGuard&) = delete;
    HashGuard& operator=(const HashGuard&) = delete;

private:
    BCRYPT_HASH_HANDLE m_handle;
};

}  // namespace

namespace Orc {

Result<std::array<uint8_t, 32>> HmacSha256(BufferView key, BufferView data)
{
    if (key.size() == 0)
    {
        Log::Debug(L"HmacSha256: refusing an empty key");
        return SystemError(E_INVALIDARG);
    }

    if (key.size() > ULONG_MAX || data.size() > ULONG_MAX)
    {
        Log::Debug(L"HmacSha256: key or data larger than a ULONG");
        return SystemError(E_INVALIDARG);
    }

    std::error_code ec;
    BCRYPT_ALG_HANDLE algorithm = NULL;
    NTSTATUS status =
        BCryptApi::OpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG, ec);
    if (ec)
    {
        return ec;  // BCryptApi logged which entry point or DLL was missing
    }

    if (status < 0)
    {
        auto statusError = SystemError(HRESULT_FROM_NT(status));
        Log::Debug(L"HmacSha256: BCryptOpenAlgorithmProvider failed [{}]", statusError);
        return statusError;
    }

    const AlgorithmGuard algorithmGuard(algorithm);

    BCRYPT_HASH_HANDLE hash = NULL;
    status = BCryptApi::CreateHash(
        algorithm,
        &hash,
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key.data())),
        static_cast<ULONG>(key.size()),
        ec);
    if (ec)
    {
        return ec;
    }

    if (status < 0)
    {
        auto statusError = SystemError(HRESULT_FROM_NT(status));
        Log::Debug(L"HmacSha256: BCryptCreateHash failed [{}]", statusError);
        return statusError;
    }

    const HashGuard hashGuard(hash);

    if (data.size() > 0)
    {
        status = BCryptApi::HashData(
            hash, const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())), static_cast<ULONG>(data.size()), ec);
        if (ec)
        {
            return ec;
        }

        if (status < 0)
        {
            auto statusError = SystemError(HRESULT_FROM_NT(status));
            Log::Debug(L"HmacSha256: BCryptHashData failed [{}]", statusError);
            return statusError;
        }
    }

    std::array<uint8_t, 32> digest = {};
    status = BCryptApi::FinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), ec);
    if (ec)
    {
        return ec;
    }

    if (status < 0)
    {
        auto statusError = SystemError(HRESULT_FROM_NT(status));
        Log::Debug(L"HmacSha256: BCryptFinishHash failed [{}]", statusError);
        return statusError;
    }

    return digest;
}

}  // namespace Orc
