//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#include <atlbase.h>

#pragma managed(push, off)

namespace Orc {

class COMExtension : public ExtensionLibrary
{
public:
    COMExtension(const std::wstring& strKeyword, const std::wstring& strX86LibRef, const std::wstring& strX64LibRef)
        : ExtensionLibrary(strKeyword, strX86LibRef, strX64LibRef) {};

    virtual std::pair<HRESULT, HINSTANCE> LoadThisLibrary(const std::wstring& strLibFile);

    virtual void FreeThisLibrary(HINSTANCE hInstance)
    {
        CoFreeLibrary(hInstance);
        return;
    }

    STDMETHOD(Initialize)();

    HRESULT AddClassFactory(CLSID clsid);

    template <class T>
    HRESULT CreateInstance(CLSID clsid, CComPtr<T>& pInterface)
    {
        auto factory = GetClassFactory(clsid);
        if (factory == nullptr)
            return CLASS_E_CLASSNOTAVAILABLE;

        return factory->CreateInstance(NULL, __uuidof(T), (LPVOID*)&pInterface);
    }

    virtual ~COMExtension();

private:
    const CComPtr<IClassFactory>& GetClassFactory(CLSID clsid);

    HRESULT(WINAPI* m_DllGetClassObject)(_In_ REFCLSID rclsid, _In_ REFIID riid, _Out_ LPVOID* ppv) = nullptr;

    std::vector<std::pair<CLSID, CComPtr<IClassFactory>>> m_ClassFactories;
};

}  // namespace Orc

#pragma managed(pop)
