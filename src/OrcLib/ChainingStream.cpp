//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "ChainingStream.h"

#include "CryptoHashStream.h"
#include "XORStream.h"

using namespace std;
using namespace Orc;

ChainingStream::~ChainingStream(void) {}

std::shared_ptr<ByteStream> ChainingStream::_GetHashStream()
{
    shared_ptr<CryptoHashStream> hs = std::dynamic_pointer_cast<CryptoHashStream>(m_pChainedStream);
    if (hs)
        return std::dynamic_pointer_cast<ByteStream>(hs);
    else if (m_pChainedStream)
        return m_pChainedStream->_GetHashStream();
    else
        return nullptr;
}

std::shared_ptr<ByteStream> ChainingStream::_GetXORStream()
{
    shared_ptr<XORStream> xs = std::dynamic_pointer_cast<XORStream>(m_pChainedStream);
    if (xs)
        return std::dynamic_pointer_cast<ByteStream>(xs);
    else if (m_pChainedStream)
        return m_pChainedStream->_GetXORStream();
    else
        return nullptr;
}
