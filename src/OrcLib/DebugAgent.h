//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <memory>

#include "Robustness.h"

#include "DbgHelpLibrary.h"

#pragma managed(push, off)

namespace Orc {

struct CreateProcessArgs
{

    LPCTSTR ImageFile;
    LPCTSTR CmdLine;

    LPSECURITY_ATTRIBUTES lpProcessAttributes;
    LPSECURITY_ATTRIBUTES lpThreadAttributes;

    BOOL bInheritHandles;
    DWORD dwCreationFlags;
    LPVOID lpEnvironment;
    LPCTSTR lpCurrentDirectory;

    LPSTARTUPINFO lpStartupInfo;
    LPPROCESS_INFORMATION lpProcessInformation;
};

class DebugAgent : public std::enable_shared_from_this<DebugAgent>
{

private:
    std::wstring m_strDirectory;
    std::wstring m_strKeyword;

    std::vector<std::wstring> m_Dumps;

    DWORD m_dwProcessID;
    DEBUG_EVENT m_Event;

    std::shared_ptr<DbgHelpLibrary> m_dbghelp;

    static DWORD WINAPI StaticDebugLoopProc(__in LPVOID lpParameter);

    HRESULT CreateMinidump(DEBUG_EVENT& debug_event);

    void DebugLoop();

    DebugAgent(const std::wstring& strDirectory, const std::wstring& strKeyword);

public:
    static std::shared_ptr<DebugAgent>
    DebugProcess(DWORD dwProcessID, const std::wstring& strDirectory, const std::wstring& strKeyword);

    const std::vector<std::wstring>& GetDumpList() const { return m_Dumps; };

    ~DebugAgent(void);
};

}  // namespace Orc
#pragma managed(pop)
