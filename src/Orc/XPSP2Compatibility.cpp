//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// GetLogicalProcessorInformationXP implementation provided by Michael Chourdakis of TurboIRC.COM
BOOL GetLogicalProcessorInformationXP(__out PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, __inout PDWORD dwLength)
{
    if (!dwLength)
        return 0;

    if (*dwLength < sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        *dwLength = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        return FALSE;
    }

    if (Buffer == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION& g1 = Buffer[0];
    g1.ProcessorMask = 0x1;
    g1.Relationship = RelationProcessorCore;
    g1.ProcessorCore.Flags = 0;
    *dwLength = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    SetLastError(0);
    return TRUE;
}

typedef BOOL(WINAPI* pGetLogicalProcessorInformation)(
    __out PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer,
    __inout PDWORD ReturnLength);

// GetLogicalProcessorInformation available on XP SP3 and above but not XP SP2
extern "C" BOOL WINAPI
AfxGetLogicalProcessorInformation(__out PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, __inout PDWORD ReturnLength)
{
    static pGetLogicalProcessorInformation GetLogicalProcessorInformation_p = NULL;
    static BOOL looked = FALSE;

    if (!looked && !GetLogicalProcessorInformation_p)
    {
        HMODULE mod = GetModuleHandle(_T("KERNEL32.DLL"));
        if (mod)
            GetLogicalProcessorInformation_p =
                (pGetLogicalProcessorInformation)GetProcAddress(mod, "GetLogicalProcessorInformation");
        else
            looked = TRUE;
    }
    if (GetLogicalProcessorInformation_p)
        return GetLogicalProcessorInformation_p(Buffer, ReturnLength);
    else
        return GetLogicalProcessorInformationXP(Buffer, ReturnLength);
}