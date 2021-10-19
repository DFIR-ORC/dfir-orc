//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#include <Xmllite.h>

#pragma managed(push, off)

namespace Orc {

using namespace std::string_literals;

class XmlLiteExtension : public ExtensionLibrary
{
private:
    HRESULT(WINAPI* m_CreateXmlWriter)
    (__in REFIID riid, __out void** ppvObject, __in_opt IMalloc* pMalloc) = nullptr;
    HRESULT(WINAPI* m_CreateXmlWriterOutputWithEncodingName)
    (__in ISequentialStream* pStream,
     __in IMalloc* pMalloc,
     __in const WCHAR* pwszEncodingName,
     __out IXmlWriterOutput** ppOutput) = nullptr;

    HRESULT(WINAPI* m_CreateXmlReader)(__in REFIID riid, __out void** ppvObject, __in IMalloc* pMalloc) = nullptr;
    HRESULT(WINAPI* m_CreateXmlReaderInputWithEncodingName)
    (__in IUnknown* pInputStream,
     __in IMalloc* pMalloc,
     __in const WCHAR* pwszEncodingName,
     __in BOOL fEncodingHint,
     __in const WCHAR* pwszBaseUri,
     __out IXmlReaderInput** ppInput) = nullptr;

public:
    XmlLiteExtension()
        : ExtensionLibrary(L"xmllite.dll"s, L"xmllite.dll;XMLLITE_X86DLL"s, L"xmllite.dll"s)
    {
        m_strDesiredName = L"XmlLite.dll"s;
    };

    STDMETHOD(Initialize)();

    template <typename... Args>
    auto CreateXmlReader(Args&&... args)
    {
        return COMCall(m_CreateXmlReader, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CreateXmlReaderInputWithEncodingName(Args&&... args)
    {
        return COMCall(m_CreateXmlReaderInputWithEncodingName, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CreateXmlWriter(Args&&... args)
    {
        return COMCall(m_CreateXmlWriter, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto CreateXmlWriterOutputWithEncodingName(Args&&... args)
    {
        return COMCall(m_CreateXmlWriterOutputWithEncodingName, std::forward<Args>(args)...);
    }

    static HRESULT LogError(HRESULT err, IXmlReader* pReader = nullptr);

    ~XmlLiteExtension();
};

}  // namespace Orc
#pragma managed(pop)
