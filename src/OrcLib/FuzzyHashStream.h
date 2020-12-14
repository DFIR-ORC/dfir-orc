//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "HashStream.h"

#include "FuzzyHashStreamAlgorithm.h"
#include "Output/Text/Fmt/FuzzyHashStreamAlgorithm.h"

#pragma managed(push, off)

class Tlsh;
struct fuzzy_state;

namespace Orc {

class FuzzyHashStream : public HashStream
{
public:
    using Algorithm = FuzzyHashStreamAlgorithm;

    FuzzyHashStream();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    // HashStream Specifics

    STDMETHOD(OpenToRead(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream));
    STDMETHOD(OpenToWrite(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream));
    STDMETHOD(Close());

    STDMETHOD(ResetHash)(bool bContinue = false);
    STDMETHOD(HashData)(LPBYTE pBuffer, DWORD dwBytesToHash);

    STDMETHOD(GetHash)(Algorithm alg, CBinaryBuffer& Hash);
    STDMETHOD(GetHash)(Algorithm alg, std::wstring& Hash);

    STDMETHOD(GetSSDeep)(CBinaryBuffer& hash) { return GetHash(FuzzyHashStream::Algorithm::SSDeep, hash); };
    STDMETHOD(GetTLSH)(CBinaryBuffer& hash) { return GetHash(FuzzyHashStream::Algorithm::TLSH, hash); };

    static Algorithm GetSupportedAlgorithm(LPCWSTR szAlgo);
    static std::wstring GetSupportedAlgorithm(Algorithm algs);

    virtual ~FuzzyHashStream();

protected:
    Algorithm m_Algorithms = Algorithm::Undefined;

#ifdef ORC_BUILD_TLSH
    std::unique_ptr<Tlsh> m_tlsh;
#endif  // ORC_BUILD_TLSH

    struct fuzzy_state* m_ssdeep = nullptr;
};

}  // namespace Orc

#pragma managed(pop)
