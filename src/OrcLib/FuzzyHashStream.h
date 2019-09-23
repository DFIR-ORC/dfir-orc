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
    enum SupportedAlgorithm
    {
        Undefined = 0,
        SSDeep = 1 << 0,
        TLSH = 1 << 1
    };

    FuzzyHashStream(logger pLog);

    // HashStream Specifics

    STDMETHOD(OpenToRead(SupportedAlgorithm algs, const std::shared_ptr<ByteStream>& pChainedStream));
    STDMETHOD(OpenToWrite(SupportedAlgorithm algs, const std::shared_ptr<ByteStream>& pChainedStream));
    STDMETHOD(Close());

    STDMETHOD(ResetHash)(bool bContinue = false);
    STDMETHOD(HashData)(LPBYTE pBuffer, DWORD dwBytesToHash);

    STDMETHOD(GetHash)(SupportedAlgorithm alg, CBinaryBuffer& Hash);
    STDMETHOD(GetHash)(SupportedAlgorithm alg, std::wstring& Hash);

    STDMETHOD(GetSSDeep)(CBinaryBuffer& hash) { return GetHash(FuzzyHashStream::SSDeep, hash); };
    STDMETHOD(GetTLSH)(CBinaryBuffer& hash) { return GetHash(FuzzyHashStream::TLSH, hash); };

    static SupportedAlgorithm GetSupportedAlgorithm(LPCWSTR szAlgo);
    static std::wstring GetSupportedAlgorithm(SupportedAlgorithm algs);

    virtual ~FuzzyHashStream();

protected:
    SupportedAlgorithm m_Algorithms = Undefined;
    std::unique_ptr<Tlsh> m_tlsh;
    struct fuzzy_state* m_ssdeep = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
