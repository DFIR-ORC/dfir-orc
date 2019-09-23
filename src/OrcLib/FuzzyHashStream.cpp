//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "FuzzyHashStream.h"

#include "WideAnsi.h"

#include "tlsh/tlsh.h"

#ifdef ORC_BUILD_SSDEEP
#    include "ssdeep/fuzzy.h"
#endif

using namespace Orc;

FuzzyHashStream::SupportedAlgorithm FuzzyHashStream::GetSupportedAlgorithm(LPCWSTR szAlgo)
{
    if (!_wcsnicmp(szAlgo, L"ssdeep", wcslen(L"ssdeep")))
    {
        return FuzzyHashStream::SupportedAlgorithm::SSDeep;
    }
    if (!_wcsnicmp(szAlgo, L"tlsh", wcslen(L"tlsh")))
    {
        return FuzzyHashStream::SupportedAlgorithm::TLSH;
    }
    return FuzzyHashStream::SupportedAlgorithm::Undefined;
}

std::wstring FuzzyHashStream::GetSupportedAlgorithm(SupportedAlgorithm algs)
{
    std::wstring retval;

    retval.reserve(16);

    if (algs & SupportedAlgorithm::SSDeep)
    {
        retval.append(L"SSDeep");
    }
    if (algs & SupportedAlgorithm::TLSH)
    {
        if (retval.empty())
            retval.append(L"TLSH");
        else
            retval.append(L",TLSH");
    }
    return retval;
}

FuzzyHashStream::FuzzyHashStream(logger pLog)
    : HashStream(std::move(pLog))
{
}

HRESULT FuzzyHashStream::OpenToRead(SupportedAlgorithm algs, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;
    if (pChainedStream == nullptr)
        return E_POINTER;

    if (pChainedStream->IsOpen() != S_OK)
    {
        log::Error(_L_, E_FAIL, L"Chained stream to FuzzyHashStream must be opened\r\n");
        return E_FAIL;
    }

    m_Algorithms = algs;
    m_pChainedStream = pChainedStream;
    if (FAILED(hr = ResetHash(true)))
        return hr;
    return S_OK;
}

HRESULT FuzzyHashStream::OpenToWrite(SupportedAlgorithm algs, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;

    if (pChainedStream != nullptr && pChainedStream->IsOpen() != S_OK)
    {
        log::Error(_L_, E_FAIL, L"Chained stream to FuzzyHashStream must be opened if provided\r\n");
        return E_FAIL;
    }

    m_bWriteOnly = true;
    m_Algorithms = algs;
    m_pChainedStream = pChainedStream;
    if (FAILED(hr = ResetHash(true)))
        return hr;
    return S_OK;
}

STDMETHODIMP FuzzyHashStream::Close()
{
    if (m_tlsh)
    {
        m_tlsh->final();
    }

    return HashStream::Close();
}

HRESULT FuzzyHashStream::ResetHash(bool bContinue)
{
    if (m_tlsh)
    {
        m_tlsh->reset();
    }

#ifdef ORC_BUILD_SSDEEP
    if (m_ssdeep)
    {
        fuzzy_free(m_ssdeep);
        m_ssdeep = nullptr;
    }

    if (m_Algorithms & FuzzyHashStream::SSDeep)
    {
        m_ssdeep = fuzzy_new();
    }
#endif  // ORC_BUILD_SSDEEP

    if (m_Algorithms & FuzzyHashStream::TLSH)
    {
        m_tlsh = std::make_unique<Tlsh>();
    }
    m_bHashIsValid = true;
    return S_OK;
}

HRESULT FuzzyHashStream::HashData(LPBYTE pBuffer, DWORD dwBytesToHash)
{
    if (m_tlsh)
    {
        m_tlsh->update(pBuffer, dwBytesToHash);
    }

#ifdef ORC_BUILD_SSDEEP
    if (m_ssdeep)
    {
        if (fuzzy_update(m_ssdeep, pBuffer, dwBytesToHash))
            return E_FAIL;
    }
#endif  // ORC_BUILD_SSDEEP

    return S_OK;
}

HRESULT FuzzyHashStream::GetHash(SupportedAlgorithm alg, CBinaryBuffer& Hash)
{
    if (m_bHashIsValid)
    {
        switch (alg)
        {
#ifdef ORC_BUILD_SSDEEP
            case FuzzyHashStream::SSDeep:
                if (m_Algorithms & FuzzyHashStream::SSDeep && m_ssdeep)
                {
                    Hash.SetCount(FUZZY_MAX_RESULT);
                    Hash.ZeroMe();
                    if (fuzzy_digest(m_ssdeep, Hash.GetP<char>(), 0L))
                        return E_FAIL;
                    return S_OK;
                }
                break;
#endif  // ORC_BUILD_SSDEEP

            case FuzzyHashStream::TLSH:
                if (m_Algorithms & FuzzyHashStream::TLSH && m_tlsh)
                {
                    if (!m_tlsh->isValid())
                    {
                        m_tlsh->final();
                    }
                    if (m_tlsh->isValid())
                    {
                        Hash.SetCount(TLSH_STRING_BUFFER_LEN);
                        Hash.ZeroMe();
                        m_tlsh->getHash(Hash.GetP<char>(), TLSH_STRING_BUFFER_LEN);
                    }
                    return S_OK;
                }
                break;
            default:
                return E_INVALIDARG;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT FuzzyHashStream::GetHash(SupportedAlgorithm alg, std::wstring& Hash)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;

    if (FAILED(hr = GetHash(alg, buffer)))
        return hr;

    Hash.clear();
    if (buffer.empty())
        return S_OK;

    if (FAILED(hr = AnsiToWide(_L_, buffer.GetP<CHAR>(), static_cast<DWORD>(buffer.GetCount()), Hash)))
        return hr;
    return S_OK;
}

FuzzyHashStream::~FuzzyHashStream()
{
#ifdef ORC_BUILD_SSDEEP
    if (m_ssdeep)
    {
        fuzzy_free(m_ssdeep);
        m_ssdeep = NULL;
    }
#endif  // ORC_BUILD_SSDEEP

    m_tlsh.reset();
}
