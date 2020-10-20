//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "FileStream.h"

#include "CriticalSection.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API SparseStream : public FileStream
{
public:
    SparseStream()
        : FileStream()
    {
    }
    ~SparseStream();

    HRESULT OpenFile(
        __in PCWSTR pwzPath,
        __in DWORD dwDesiredAccess,
        __in DWORD dwSharedMode,
        __in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
        __in DWORD dwCreationDisposition,
        __in DWORD dwFlagsAndAttributes,
        __in_opt HANDLE hTemplate);

    STDMETHOD(SetSize)(ULONG64 ullSize);

    STDMETHOD(GetAllocatedRanges)(std::vector<FILE_ALLOCATED_RANGE_BUFFER>& ranges);

private:
};

}  // namespace Orc

#pragma managed(pop)
