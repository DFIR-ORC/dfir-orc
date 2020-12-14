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
#include "BinaryBuffer.h"

#ifdef ORC_BUILD_TLSH
#    include "tlsh/tlsh.h"
#endif  // ORC_BUILD_TLSH

#ifdef ORC_BUILD_SSDEEP
#    include "ssdeep/fuzzy.h"
#endif  // ORC_BUILD_SSDEEP

using namespace Orc;

FuzzyHashStream::Algorithm FuzzyHashStream::GetSupportedAlgorithm(LPCWSTR szAlgo)
{
#ifdef ORC_BUILD_SSDEEP
    if (!_wcsnicmp(szAlgo, L"ssdeep", wcslen(L"ssdeep")))
    {
        return Algorithm::SSDeep;
    }
#endif  // ORC_BUILD_SSDEEP

#ifdef ORC_BUILD_TSLH
    if (!_wcsnicmp(szAlgo, L"tlsh", wcslen(L"tlsh")))
    {
        return Algorithm::TLSH;
    }
#endif  // ORC_BUILD_TLSH

    return Algorithm::Undefined;
}

std::wstring FuzzyHashStream::GetSupportedAlgorithm(Algorithm algs)
{
    std::wstring retval;

    retval.reserve(16);

#ifdef ORC_BUILD_SSDEEP
    if (HasFlag(algs, FuzzyHashStream::Algorithm::SSDeep))
    {
        retval.append(L"SSDeep");
    }
#endif  // ORC_BUILD_SSDEEP

#ifdef ORC_BUILD_TLSH
    if (HasFlag(algs, FuzzyHashStream::Algorithm::TLSH))
    {
        if (retval.empty())
            retval.append(L"TLSH");
        else
            retval.append(L",TLSH");
    }
#endif  // ORC_BUILD_TLSH

    return retval;
}

FuzzyHashStream::FuzzyHashStream()
    : HashStream()
{
}

HRESULT FuzzyHashStream::OpenToRead(FuzzyHashStream::Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;
    if (pChainedStream == nullptr)
        return E_POINTER;

    if (pChainedStream->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream to FuzzyHashStream must be opened");
        return E_FAIL;
    }

    m_Algorithms = algs;
    m_pChainedStream = pChainedStream;
    if (FAILED(hr = ResetHash(true)))
        return hr;
    return S_OK;
}

HRESULT FuzzyHashStream::OpenToWrite(FuzzyHashStream::Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;

    if (pChainedStream != nullptr && pChainedStream->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream to FuzzyHashStream must be opened if provided");
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
#ifdef ORC_BUILD_TLSH
    if (m_tlsh)
    {
        m_tlsh->final();
    }
#endif  // ORC_BUILD_TLSH

    return HashStream::Close();
}

HRESULT FuzzyHashStream::ResetHash(bool bContinue)
{
#ifdef ORC_BUILD_SSDEEP
    if (m_ssdeep)
    {
        fuzzy_free(m_ssdeep);
        m_ssdeep = nullptr;
    }

    if (HasFlag(m_Algorithms, FuzzyHashStream::Algorithm::SSDeep))
    {
        m_ssdeep = fuzzy_new();
    }
#endif  // ORC_BUILD_SSDEEP

#ifdef ORC_BUILD_TLSH
    if (m_tlsh)
    {
        m_tlsh->reset();
    }

    if (HasFlag(m_Algorithms, FuzzyHashStream::Algorithm::TLSH))
    {
        m_tlsh = std::make_unique<Tlsh>();
    }
#endif  // ORC_BUILD_TLSH

    m_bHashIsValid = true;
    return S_OK;
}

HRESULT FuzzyHashStream::HashData(LPBYTE pBuffer, DWORD dwBytesToHash)
{
#ifdef ORC_BUILD_TLSH
    if (m_tlsh)
    {
        m_tlsh->update(pBuffer, dwBytesToHash);
    }
#endif  // ORC_BUILD_TLSH

#ifdef ORC_BUILD_SSDEEP
    if (m_ssdeep)
    {
        if (fuzzy_update(m_ssdeep, pBuffer, dwBytesToHash))
            return E_FAIL;
    }
#endif  // ORC_BUILD_SSDEEP

    return S_OK;
}

HRESULT FuzzyHashStream::GetHash(FuzzyHashStream::Algorithm alg, CBinaryBuffer& Hash)
{
    if (m_bHashIsValid)
    {
        switch (alg)
        {
            case FuzzyHashStream::Algorithm::SSDeep:
#ifdef ORC_BUILD_SSDEEP
                if (HasFlag(m_Algorithms, FuzzyHashStream::Algorithm::SSDeep) && m_ssdeep)
                {
                    Hash.SetCount(FUZZY_MAX_RESULT);
                    Hash.ZeroMe();
                    if (fuzzy_digest(m_ssdeep, Hash.GetP<char>(), 0L))
                        return E_FAIL;
                    return S_OK;
                }
#endif  // ORC_BUILD_SSDEEP
                break;
            case FuzzyHashStream::Algorithm::TLSH:
#ifdef ORC_BUILD_TLSH
                if (HasFlag(m_Algorithms, FuzzyHashStream::Algorithm::TLSH) && m_tlsh)
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
#endif  // ORC_BUILD_TLSH
                break;
            default:
                return E_INVALIDARG;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT FuzzyHashStream::GetHash(Algorithm alg, std::wstring& Hash)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;

    if (FAILED(hr = GetHash(alg, buffer)))
        return hr;

    Hash.clear();
    if (buffer.empty())
        return S_OK;

    if (FAILED(hr = AnsiToWide(buffer.GetP<CHAR>(), static_cast<DWORD>(buffer.GetCount()), Hash)))
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

#ifdef ORC_BUILD_TLSH
    m_tlsh.reset();
#endif
}
