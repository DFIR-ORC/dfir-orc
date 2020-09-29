//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "MemoryStream.h"
#include "CriticalSection.h"
#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {
class ORCLIB_API ResourceStream : public MemoryStream
{

public:
    ResourceStream();
    ~ResourceStream();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    STDMETHOD(IsOpen)()
    {
        if (m_hResource)
            return S_OK;
        else
            return S_FALSE;
    };
    STDMETHOD(CanRead)() { return S_OK; };
    STDMETHOD(CanWrite)() { return S_FALSE; };
    STDMETHOD(CanSeek)() { return S_OK; };

    STDMETHOD(OpenForReadWrite)();
    STDMETHOD(OpenForReadOnly)(__in const HINSTANCE hInstance, __in WORD resourceID);
    STDMETHOD(OpenForReadOnly)(__in const HINSTANCE hInstance, __in HRSRC hRes);
    STDMETHOD(OpenForReadOnly)(__in const std::wstring& strResourceSpec);

    STDMETHOD(Close)();

protected:
    CriticalSection m_cs;
    HGLOBAL m_hFileResource = NULL;
    HRSRC m_hResource = NULL;
};
}  // namespace Orc

#pragma managed(pop)
