//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include <new.h>

#include <string>
#include <vector>
#include <algorithm>
#include <memory>

#pragma managed(push, off)

namespace Orc {

class MemoryException
{
private:
    size_t m_attempted;

public:
    MemoryException(size_t attempted)
        : m_attempted(attempted) {};

    size_t GetAttemptedSize() { return m_attempted; };
};

class SystemException
{
private:
    unsigned int m_Code;
    EXCEPTION_POINTERS* m_Pointers;

public:
    explicit SystemException(unsigned int n, EXCEPTION_POINTERS* pExp)
        : m_Code(n)
        , m_Pointers(pExp)
    {
    }
    ~SystemException() {}
    unsigned int GetExceptionCode() { return m_Code; }
    EXCEPTION_POINTERS* GetExecptionPointers() { return m_Pointers; };
};

class ConsoleException
{
private:
    DWORD m_dwCtrlType;

public:
    ConsoleException(DWORD n)
        : m_dwCtrlType(n)
    {
    }
    ~ConsoleException() {}
    DWORD CtrlType() { return m_dwCtrlType; }
};

constexpr static const unsigned int ROBUSTNESS_FIRST = (UINT)-1;

constexpr static const unsigned int ROBUSTNESS_COMMAND = 1 << 7;  // Kill the child processes first
constexpr static const unsigned int ROBUSTNESS_STOP_PROCESSING = 1
    << 6;  // Stop creating new data (i.e. stop MFT walker)
constexpr static const unsigned int ROBUSTNESS_CSV = 1 << 5;  // Flush CSV File writer
constexpr static const unsigned int ROBUSTNESS_LOG = 1 << 4;  // Flush the log file
constexpr static const unsigned int ROBUSTNESS_STATS = 1 << 3;  // Add the latest data about execution
constexpr static const unsigned int ROBUSTNESS_ARCHIVE = 1 << 2;  // Cab the files
constexpr static const unsigned int ROBUSTNESS_TEMPFILE = 1 << 1;  // Delete temp files
constexpr static const unsigned int ROBUSTNESS_UNLOAD_DLLS = 1 << 0;  // Unload extension DLLs

constexpr static const unsigned int ROBUSTNESS_LAST = 0;

class ORCLIB_API TerminationHandler
{

public:
    virtual HRESULT operator()() = 0;
    unsigned short Priority;
    std::wstring Description;

    TerminationHandler(std::wstring strDescr, short sPriority)
        : Priority(sPriority)
        , Description(std::move(strDescr))
    {
    }
};

class ORCLIB_API Robustness
{
public:
    static int handle_program_memory_depletion(size_t);
    static void seTransFunction(unsigned int u, EXCEPTION_POINTERS* pExp);
    static BOOL WINAPI ConsoleEventHandlerRoutine(__in DWORD dwCtrlType);
    static LONG WINAPI UnhandledExceptionFilter(__in struct _EXCEPTION_POINTERS* ExceptionInfo);

    static void Terminate(bool bSilent);
    static void Terminate();

    static HRESULT Initialize(const WCHAR* szProcessDescr, bool bSilent = false, bool bOverrideGobalHandlers = true);

    static void AddTerminationHandler(const std::shared_ptr<TerminationHandler>& pHandler);

    static void RemoveTerminationHandler(const std::shared_ptr<TerminationHandler>& pHandler);

    static HRESULT UnInitialize(DWORD dwTimeout);
};
}  // namespace Orc

#pragma managed(pop)
