//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "HashStream.h"

#include <memory>

#include "Utils/EnumFlags.h"

#include "CryptoUtilities.h"

#pragma managed(push, off)

namespace Orc {


class ORCLIB_API CryptoHashStream : public HashStream
{
public:
    enum class Algorithm : char
    {
        Undefined = 0,
        MD5 = 1 << 0,
        SHA1 = 1 << 1,
        SHA256 = 1 << 2
    };

protected:
    Algorithm m_Algorithms;
    HCRYPTHASH m_Sha256;
    HCRYPTHASH m_Sha1;
    HCRYPTHASH m_MD5;

    static HCRYPTPROV g_hProv;

    STDMETHOD(ResetHash(bool bContinue = false));
    STDMETHOD(HashData(LPBYTE pBuffer, DWORD dwBytesToHash));

public:
    CryptoHashStream()
        : HashStream()
        , m_Sha256(NULL)
        , m_Sha1(NULL)
        , m_MD5(NULL) {};

    ~CryptoHashStream(void);

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    // CryptoHashStream Specifics

    virtual HRESULT OpenToRead(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream);
    virtual HRESULT OpenToWrite(Algorithm algs, const std::shared_ptr<ByteStream>& pChainedStream);

    HRESULT GetHash(Algorithm alg, CBinaryBuffer& Hash);
    HRESULT GetHash(Algorithm alg, std::wstring& Hash);

    HRESULT GetSHA256(CBinaryBuffer& hash) { return GetHash(Algorithm::SHA256, hash); };
    HRESULT GetSHA1(CBinaryBuffer& hash) { return GetHash(Algorithm::SHA1, hash); };
    HRESULT GetMD5(CBinaryBuffer& hash) { return GetHash(Algorithm::MD5, hash); };

    static Algorithm GetSupportedAlgorithm(std::wstring_view svAlgo);
    static std::wstring GetSupportedAlgorithm(Algorithm algs);
};

ENABLE_BITMASK_OPERATORS(CryptoHashStream::Algorithm)

}  // namespace Orc

#pragma managed(pop)
