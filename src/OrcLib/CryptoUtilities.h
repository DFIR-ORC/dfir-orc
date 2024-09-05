//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma managed(push, off)

namespace Orc {

class CryptoUtilities
{
public:
    CryptoUtilities() = delete;

    static HRESULT AcquireContext(_Out_ HCRYPTPROV& hCryptProv);
};

}  // namespace Orc

#define BYTES_IN_MD5_HASH 16
#define BYTES_IN_SHA1_HASH 20
#define BYTES_IN_SHA256_HASH 32
#define BYTES_IN_FIRSTBYTES 16

#pragma managed(pop)
