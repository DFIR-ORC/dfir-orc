//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "ExtensionLibrary.h"

#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma warning(disable : 4091)

#include <concrt.h>

#pragma managed(push, off)

namespace Orc {

class DbgHelpLibrary : public ExtensionLibrary
{
    friend class ExtensionLibrary;

private:
    BOOL(WINAPI* m_MiniDumpWriteDump)
    (__in HANDLE hProcess,
     __in DWORD ProcessId,
     __in HANDLE hFile,
     __in MINIDUMP_TYPE DumpType,
     __in_opt PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
     __in_opt PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
     __in_opt PMINIDUMP_CALLBACK_INFORMATION CallbackParam) = nullptr;
    LPAPI_VERSION(WINAPI* m_ImagehlpApiVersion)(void) = nullptr;

    concurrency::critical_section m_cs;

public:
    DbgHelpLibrary();

    STDMETHOD(Initialize)();

    API_VERSION GetVersion();

    template <typename... Args>
    auto MiniDumpWriteDump(Args&&... args)
    {
        return Win32Call(m_MiniDumpWriteDump, std::forward<Args>(args)...);
    }

    ~DbgHelpLibrary();
};

}  // namespace Orc

#pragma managed(pop)
