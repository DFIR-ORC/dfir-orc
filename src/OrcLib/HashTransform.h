//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <agents.h>

#include "OrcLib.h"

#include "StreamMessage.h"
#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API HashTransform
{
public:
private:
    Concurrency::ITarget<CBinaryBuffer>& m_sha1Target;
    Concurrency::ITarget<CBinaryBuffer>& m_md5Target;

    HCRYPTHASH m_hSha1;
    HCRYPTHASH m_hMD5;
    bool m_bHashIsValid;

    static HCRYPTPROV g_hProv;

    HRESULT ResetHash(bool bContinue = false);
    HRESULT HashData(LPBYTE pBuffer, DWORD dwBytesToHash);
    HRESULT HasValidHash() { return m_bHashIsValid ? S_OK : S_FALSE; };

public:
    HashTransform(Concurrency::ITarget<CBinaryBuffer>& md5Target, Concurrency::ITarget<CBinaryBuffer>& sha1Target);

    std::shared_ptr<StreamMessage> operator()(const std::shared_ptr<StreamMessage>& item);

    ~HashTransform(void);
};

}  // namespace Orc

#pragma managed(pop)
