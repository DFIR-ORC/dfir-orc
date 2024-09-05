//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"

#pragma managed(push, off)

#include <accctrl.h>

namespace Orc {

HRESULT SetPrivilege(const WCHAR* szPrivilege, BOOL bEnablePrivilege);

HRESULT GetMyCurrentSID(PSID& pSid);

HRESULT GetObjectOwnerSID(SE_OBJECT_TYPE objType, HANDLE hObject, PSID& pSid);

HRESULT TakeOwnership(SE_OBJECT_TYPE objType, HANDLE hObject, PSID& pPreviousOwnerSid);

HRESULT GrantAccess(SE_OBJECT_TYPE objType, HANDLE hObject, PSID pSid, ACCESS_MASK mask);

}  // namespace Orc

#pragma managed(pop)
