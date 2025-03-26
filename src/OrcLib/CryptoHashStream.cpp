//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "CryptoHashStream.h"

#include "SystemDetails.h"

#include "CryptoUtilities.h"
#include "CaseInsensitive.h"
#include "BinaryBuffer.h"
#include "DevNullStream.h"
#include "FileStream.h"

#include <sstream>
#include <iomanip>

// CALG_SHA_256 could be undefined by 'WinCrypt.h' because of targetted WINVER
#include <WinCrypt.h>
#ifndef CALG_SHA_256
#    define ALG_SID_SHA_256 12
#    define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_256)
#endif

using namespace std;

namespace Orc {

HCRYPTPROV CryptoHashStream::g_hProv = NULL;

Result<std::wstring> Hash(const std::filesystem::path& path, CryptoHashStream::Algorithm algorithm)
{
    auto fileStream = std::make_shared<FileStream>();

    HRESULT hr = fileStream->OpenFile(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to open: '{}' [{}]", path, SystemError(hr));
        return SystemError(hr);
    }

    CryptoHashStream hashStream;
    hr = hashStream.OpenToRead(algorithm, fileStream);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to open hashstream: '{}' [{}]", path, SystemError(hr));
        return SystemError(hr);
    }

    ULONGLONG ullBytesWritten;
    DevNullStream devNull;
    hr = hashStream.CopyTo(devNull, &ullBytesWritten);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to consume stream: '{}' [{}]", path, SystemError(hr));
        return SystemError(hr);
    }

    std::wstring hash;
    hr = hashStream.GetHash(algorithm, hash);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to get {}: '{}' [{}]", algorithm, path, SystemError(hr));
        return SystemError(hr);
    }

    Log::Debug(L"Hash for '{}': {}:{}", path, algorithm, hash);
    return hash;
}

CryptoHashStream::~CryptoHashStream(void)
{
    Close();
    if (m_MD5 != NULL)
        CryptDestroyHash(m_MD5);
    if (m_Sha1 != NULL)
        CryptDestroyHash(m_Sha1);
    if (m_Sha256 != NULL)
        CryptDestroyHash(m_Sha256);
    m_MD5 = m_Sha1 = m_Sha256 = NULL;
    m_bHashIsValid = false;
}

HRESULT CryptoHashStream::OpenToRead(Algorithm algs, const std::shared_ptr<ByteStream>& pChained)
{
    HRESULT hr = E_FAIL;
    if (pChained == nullptr)
        return E_POINTER;

    if (pChained->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream to HashStream must be opened");
        return E_FAIL;
    }

    m_Algorithms = algs;
    m_pChainedStream = pChained;
    if (FAILED(hr = ResetHash(true)))
        return hr;
    return S_OK;
}

HRESULT CryptoHashStream::OpenToWrite(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;

    if (pChainedStream != nullptr && pChainedStream->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream to HashStream must be opened if provided");
        return E_FAIL;
    }

    m_bWriteOnly = true;
    m_Algorithms = algs;
    m_pChainedStream = pChainedStream;
    if (FAILED(hr = ResetHash(true)))
        return hr;
    return S_OK;
}

HRESULT CryptoHashStream::ResetHash(bool bContinue)
{
    HRESULT hr = S_OK;

    if (m_MD5 != NULL)
        CryptDestroyHash(m_MD5);
    if (m_Sha1 != NULL)
        CryptDestroyHash(m_Sha1);
    if (m_Sha256 != NULL)
        CryptDestroyHash(m_Sha256);

    m_MD5 = m_Sha1 = m_Sha256 = NULL;
    m_bHashIsValid = false;

    if (bContinue)
    {
        if (g_hProv == NULL)
        {
            // Acquire the best available crypto provider
            if (FAILED(hr = CryptoUtilities::AcquireContext(g_hProv)))
            {
                Log::Error(L"Failed to initialize providers [{}]", SystemError(hr));
                return hr;
            }
        }
        m_bHashIsValid = true;

        if (HasFlag(m_Algorithms, Algorithm::MD5) && !CryptCreateHash(g_hProv, CALG_MD5, 0, 0, &m_MD5))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to initialise MD5 hash [{}]", SystemError(hr));
        }

        if (HasFlag(m_Algorithms, Algorithm::SHA1) && !CryptCreateHash(g_hProv, CALG_SHA1, 0, 0, &m_Sha1))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to initialise SHA1 hash [{}]", SystemError(hr));
        }
        if (HasFlag(m_Algorithms, Algorithm::SHA256) && !CryptCreateHash(g_hProv, CALG_SHA_256, 0, 0, &m_Sha256))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug("Failed to initialise SHA256 hash [{}]", SystemError(hr));
        }
    }
    return S_OK;
}

HRESULT CryptoHashStream::HashData(LPBYTE pBuffer, DWORD dwBytesToHash)
{
    if (m_bHashIsValid)
    {
        if (m_MD5)
            if (!CryptHashData(m_MD5, pBuffer, dwBytesToHash, 0))
                return HRESULT_FROM_WIN32(GetLastError());
        if (m_Sha1)
            if (!CryptHashData(m_Sha1, pBuffer, dwBytesToHash, 0))
                return HRESULT_FROM_WIN32(GetLastError());
        if (m_Sha256)
            if (!CryptHashData(m_Sha256, pBuffer, dwBytesToHash, 0))
                return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

HRESULT CryptoHashStream::GetHash(Algorithm alg, CBinaryBuffer& hash)
{
    if (m_bHashIsValid)
    {
        HCRYPTHASH hHash = NULL;
        DWORD cbHash = 0L;
        switch (alg)
        {
            case Algorithm::MD5:
                cbHash = BYTES_IN_MD5_HASH;
                hHash = m_MD5;
                break;
            case Algorithm::SHA1:
                cbHash = BYTES_IN_SHA1_HASH;
                hHash = m_Sha1;
                break;
            case Algorithm::SHA256:
                cbHash = BYTES_IN_SHA256_HASH;
                hHash = m_Sha256;
                break;
            default:
                return E_INVALIDARG;
        }

        if (hHash == NULL)
        {
            hash.RemoveAll();
            return MK_E_UNAVAILABLE;
        }

        hash.SetCount(cbHash);
        hash.ZeroMe();
        if (!CryptGetHashParam(hHash, HP_HASHVAL, hash.GetData(), &cbHash, 0))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    else
        hash.SetCount(0);
    return S_OK;
}

HRESULT CryptoHashStream::GetHash(Algorithm alg, std::wstring& Hash)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer bin_hash;

    if (FAILED(GetHash(alg, bin_hash)))
        return hr;

    std::wstringstream stream;
    stream << hex << std::uppercase << setfill(L'0');
    for (DWORD i = 0; i < bin_hash.GetCount(); i++)
    {
        stream << std::setw(2) << bin_hash.Get<BYTE>(i);
    }
    Hash = stream.str();
    return S_OK;
}

CryptoHashStream::Algorithm CryptoHashStream::GetSupportedAlgorithm(std::wstring_view svAlgo)
{
    using namespace std::string_view_literals;

    constexpr auto MD5 = L"MD5"sv;
    constexpr auto SHA1 = L"SHA1"sv;
    constexpr auto SHA256 = L"SHA256"sv;

    if (equalCaseInsensitive(svAlgo, MD5, MD5.size()))
    {
        return Algorithm::MD5;
    }
    if (equalCaseInsensitive(svAlgo, SHA1, SHA1.size()))
    {
        return Algorithm::SHA1;
    }
    if (equalCaseInsensitive(svAlgo, SHA256, SHA256.size()))
    {
        return Algorithm::SHA256;
    }
    return Algorithm::Undefined;
}

std::wstring CryptoHashStream::GetSupportedAlgorithm(Algorithm algs)
{
    using namespace std::string_view_literals;
    std::wstring retval;

    retval.reserve(16);

    if (HasFlag(algs, Algorithm::MD5))
    {
        retval.append(L"MD5"sv);
    }
    if (HasFlag(algs, Algorithm::SHA1))
    {
        if (retval.empty())
            retval.append(L"SHA1"sv);
        else
            retval.append(L",SHA1"sv);
    }
    if (HasFlag(algs, Algorithm::SHA256))
    {
        if (retval.empty())
            retval.append(L"SHA256"sv);
        else
            retval.append(L",SHA256"sv);
    }

    return retval;
}

}  // namespace Orc
