//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ChainingStream : public ByteStream
{
protected:
    std::shared_ptr<ByteStream> m_pChainedStream;

public:
    ChainingStream()
        : ByteStream() {};
    virtual ~ChainingStream(void);

    STDMETHOD(IsOpen)()
    {
        if (m_pChainedStream == NULL)
            return S_FALSE;
        return m_pChainedStream->IsOpen();
    };
    STDMETHOD(CanRead)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanRead();
    };
    STDMETHOD(CanWrite)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanWrite();
    };
    STDMETHOD(CanSeek)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanSeek();
    };

    virtual std::shared_ptr<ByteStream> _GetHashStream();
};

}  // namespace Orc

#pragma managed(pop)
