//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ExtensionLibrary.h"

#pragma managed(push, off)

namespace Orc {

class MSIExtension : public ExtensionLibrary
{
private:
    HRESULT(WINAPI* m_MsiGetFileSignatureInformation)
    (_In_ LPCTSTR szSignedObjectPath,
     _In_ DWORD dwFlags,
     _Out_ PCCERT_CONTEXT* ppcCertContext,
     _Out_ BYTE* pbHashData,
     _Inout_ DWORD* pcbHashData) = nullptr;

public:
    MSIExtension(logger pLog)
        : ExtensionLibrary(std::move(pLog), L"msi.dll", L"msi.dll", L"msi.dll") {};

    STDMETHOD(Initialize)();

    template <typename... Args>
    auto MsiGetFileSignatureInformation(Args&&... args)
    {
        if (FAILED(Initialize()))
            return E_FAIL;
        return m_MsiGetFileSignatureInformation(std::forward<Args>(args)...);
    }

    ~MSIExtension();
};

}  // namespace Orc

#pragma managed(pop)
