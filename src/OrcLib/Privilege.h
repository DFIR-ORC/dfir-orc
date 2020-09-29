//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "OrcLib.h"

#pragma managed(push, off)

namespace Orc {

ORCLIB_API HRESULT SetPrivilege(WCHAR* szPrivilege, BOOL bEnablePrivilege);

ORCLIB_API HRESULT GetMyCurrentSID(PSID& pSid);

ORCLIB_API HRESULT GetObjectOwnerSID(SE_OBJECT_TYPE objType, HANDLE hObject, PSID& pSid);

ORCLIB_API HRESULT TakeOwnership(SE_OBJECT_TYPE objType, HANDLE hObject, PSID& pPreviousOwnerSid);

ORCLIB_API HRESULT GrantAccess(SE_OBJECT_TYPE objType, HANDLE hObject, PSID pSid, ACCESS_MASK mask);

}  // namespace Orc

#pragma managed(pop)
