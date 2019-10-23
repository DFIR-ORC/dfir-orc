//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "HashStream.h"

#pragma managed(push, off)

class Tlsh;
struct fuzzy_state;

namespace Orc {

class LogFileWriter;

class FuzzyHashStream : public HashStream
{
public:
    enum Algorithm
    {
        Undefined = 0,
        SSDeep = 1 << 0,
        TLSH = 1 << 1
    };

    friend Algorithm& operator^=(Algorithm& left, const Algorithm rigth)
    {
        left = static_cast<Algorithm>(static_cast<char>(left) ^ static_cast<char>(rigth));
        return left;
    }
    friend Algorithm& operator|=(Algorithm& left, const Algorithm rigth)
    {
        left = static_cast<Algorithm>(static_cast<char>(left) | static_cast<char>(rigth));
        return left;
    }

    friend Algorithm operator^(const Algorithm left, const Algorithm rigth)
    {
        return static_cast<Algorithm>(static_cast<char>(left) ^ static_cast<char>(rigth));
    }
    friend Algorithm operator|(const Algorithm left, const Algorithm rigth)
    {
        return static_cast<Algorithm>(static_cast<char>(left) | static_cast<char>(rigth));
    }
    friend Algorithm operator&(const Algorithm left, const Algorithm rigth)
    {
        return static_cast<Algorithm>(static_cast<char>(left) & static_cast<char>(rigth));
    }

    FuzzyHashStream(logger pLog);

    // HashStream Specifics

    STDMETHOD(OpenToRead(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream));
    STDMETHOD(OpenToWrite(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream));
    STDMETHOD(Close());

    STDMETHOD(ResetHash)(bool bContinue = false);
    STDMETHOD(HashData)(LPBYTE pBuffer, DWORD dwBytesToHash);

    STDMETHOD(GetHash)(Algorithm alg, CBinaryBuffer& Hash);
    STDMETHOD(GetHash)(Algorithm alg, std::wstring& Hash);

    STDMETHOD(GetSSDeep)(CBinaryBuffer& hash) { return GetHash(FuzzyHashStream::SSDeep, hash); };
    STDMETHOD(GetTLSH)(CBinaryBuffer& hash) { return GetHash(FuzzyHashStream::TLSH, hash); };

    static Algorithm GetSupportedAlgorithm(LPCWSTR szAlgo);
    static std::wstring GetSupportedAlgorithm(Algorithm algs);

    virtual ~FuzzyHashStream();

protected:
    Algorithm m_Algorithms = Undefined;
    std::unique_ptr<Tlsh> m_tlsh;
    struct fuzzy_state* m_ssdeep = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
