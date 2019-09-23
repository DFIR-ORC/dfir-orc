//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "SecurityDescriptor.h"

using namespace Orc;

HRESULT SecurityDescriptor::ConvertFromSDDL(LPCWSTR szSDDL)
{
    HRESULT hr = E_FAIL;

    PSECURITY_DESCRIPTOR retval = NULL;
    ULONG ulLength = 0L;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(szSDDL, SDDL_REVISION_1, &retval, &ulLength))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to convert string \"%s\" into a valid security descriptor",
            szSDDL);
        return hr;
    }

    m_SD = retval;

    return S_OK;
}
