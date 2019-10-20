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

#include "CryptoUtilities.h"

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API CryptoHashStream : public HashStream
{
public:

    enum Algorithm : char
    {
        Undefined = 0,
        MD5 = 1 << 0,
        SHA1 = 1 << 1,
        SHA256 = 1 << 2
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



protected:
    Algorithm m_Algorithms;
    HCRYPTHASH m_Sha1;
    HCRYPTHASH m_Sha256;
    HCRYPTHASH m_MD5;

    static HCRYPTPROV g_hProv;

    STDMETHOD(ResetHash(bool bContinue = false));
    STDMETHOD(HashData(LPBYTE pBuffer, DWORD dwBytesToHash));

public:
    CryptoHashStream(logger pLog)
        : HashStream(std::move(pLog))
        , m_Sha256(NULL)
        , m_Sha1(NULL)
        , m_MD5(NULL) {};

    ~CryptoHashStream(void);

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

}  // namespace Orc

#pragma managed(pop)
