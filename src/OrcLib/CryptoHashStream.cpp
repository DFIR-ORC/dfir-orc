//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "CryptoHashStream.h"

#include "LogFileWriter.h"

#include "SystemDetails.h"

#include "CryptoUtilities.h"
#include "CaseInsensitive.h"

#include <sstream>
#include <iomanip>

using namespace std;

using namespace Orc;

HCRYPTPROV CryptoHashStream::g_hProv = NULL;

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
        log::Error(_L_, E_FAIL, L"Chained stream to HashStream must be opened\r\n");
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
        log::Error(_L_, E_FAIL, L"Chained stream to HashStream must be opened if provided\r\n");
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
            if (FAILED(hr = CryptoUtilities::AcquireContext(_L_, g_hProv)))
            {
                log::Error(_L_, hr, L"Failed to initalize providers\r\n");
                return hr;
            }
        }
        m_bHashIsValid = true;

        if ((m_Algorithms & MD5) && !CryptCreateHash(g_hProv, CALG_MD5, 0, 0, &m_MD5))
        {
            log::Verbose(_L_, L"Failed to initialise MD5 hash (hr=0x%lx)\r\n", hr = HRESULT_FROM_WIN32(GetLastError()));
        }
        if ((m_Algorithms & SHA1) && !CryptCreateHash(g_hProv, CALG_SHA1, 0, 0, &m_Sha1))
        {
            log::Verbose(
                _L_, L"Failed to initialise SHA1 hash (hr=0x%lx)\r\n", hr = HRESULT_FROM_WIN32(GetLastError()));
        }
        if ((m_Algorithms & SHA256) && !CryptCreateHash(g_hProv, CALG_SHA_256, 0, 0, &m_Sha256))
        {
            log::Verbose(
                _L_, L"Failed to initialise SHA256 hash (hr=0x%lx)\r\n", hr = HRESULT_FROM_WIN32(GetLastError()));
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
            case MD5:
                cbHash = BYTES_IN_MD5_HASH;
                hHash = m_MD5;
                break;
            case SHA1:
                cbHash = BYTES_IN_SHA1_HASH;
                hHash = m_Sha1;
                break;
            case SHA256:
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

    if (algs & Algorithm::MD5)
    {
        retval.append(L"MD5"sv);
    }
    if (algs & Algorithm::SHA1)
    {
        if (retval.empty())
            retval.append(L"SHA1"sv);
        else
            retval.append(L",SHA1"sv);
    }
    if (algs & Algorithm::SHA256)
    {
        if (retval.empty())
            retval.append(L"SHA256"sv);
        else
            retval.append(L",SHA256"sv);
    }

    return retval;
}
