//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "SecurityDescriptor.h"

#include "Log/Log.h"

using namespace Orc;

HRESULT SecurityDescriptor::ConvertFromSDDL(LPCWSTR szSDDL)
{
    HRESULT hr = E_FAIL;

    PSECURITY_DESCRIPTOR retval = NULL;
    ULONG ulLength = 0L;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(szSDDL, SDDL_REVISION_1, &retval, &ulLength))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to convert string '{}' into a valid security descriptor (code: {:#x})", szSDDL, hr);
        return hr;
    }

    m_SD = retval;

    return S_OK;
}
