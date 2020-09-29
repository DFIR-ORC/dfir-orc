//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma managed(push, off)

namespace Orc {

class SecurityDescriptor
{
private:
    PSECURITY_DESCRIPTOR m_SD = nullptr;

public:
    SecurityDescriptor() {}

    HRESULT ConvertFromSDDL(LPCWSTR szSDDL);

    PSECURITY_DESCRIPTOR GetSecurityDescriptor() { return m_SD; }

    HRESULT FreeSecurityDescritor()
    {
        LPVOID pSD = m_SD;
        m_SD = nullptr;

        if (pSD != nullptr)
        {
            LocalFree(pSD);
        }
        return S_OK;
    }

    ~SecurityDescriptor() { FreeSecurityDescritor(); }
};

}  // namespace Orc

#pragma managed(pop)
