//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "HashStream.h"

#include <memory>
#include <filesystem>

#include "CryptoUtilities.h"
#include "CryptoHashStreamAlgorithm.h"
#include "Text/Fmt/CryptoHashStreamAlgorithm.h"
#include "Utils/Result.h"

#pragma managed(push, off)

namespace Orc {

class CryptoHashStream : public HashStream
{
public:
    using Algorithm = CryptoHashStreamAlgorithm;

    CryptoHashStream()
        : HashStream()
        , m_Algorithms(Algorithm::Undefined)
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

protected:
    Algorithm m_Algorithms;
    HCRYPTHASH m_Sha256;
    HCRYPTHASH m_Sha1;
    HCRYPTHASH m_MD5;

    static HCRYPTPROV g_hProv;

    STDMETHOD(ResetHash(bool bContinue = false));
    STDMETHOD(HashData(LPBYTE pBuffer, DWORD dwBytesToHash));
};

Result<std::wstring> Hash(const std::filesystem::path& path, CryptoHashStream::Algorithm algorithm);

}  // namespace Orc

#pragma managed(pop)
