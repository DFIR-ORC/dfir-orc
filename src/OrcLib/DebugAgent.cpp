//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <memory>

#include "DebugAgent.h"
#include "Temporary.h"

#include "DbgHelpLibrary.h"

#include <spdlog/spdlog.h>

using namespace std;

using namespace Orc;

DebugAgent::DebugAgent(const std::wstring& strDirectory, const std::wstring& strKeyword)
    : m_strDirectory(strDirectory)
    , m_strKeyword(strKeyword)
{
    ZeroMemory(&m_Event, sizeof(DEBUG_EVENT));
    m_dbghelp = ExtensionLibrary::GetLibrary<DbgHelpLibrary>();
}

DWORD WINAPI DebugAgent::StaticDebugLoopProc(__in LPVOID lpParameter)
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<DebugAgent> pThis = ((DebugAgent*)lpParameter)->shared_from_this();

    if (!DebugActiveProcess(pThis->m_dwProcessID))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error("Failed to attach debugger to running process (pid: {}, code: {:#x})", pThis->m_dwProcessID, hr);
        return hr;
    }
    else
    {
        spdlog::debug("Attached debugger to running process (pid: {})", pThis->m_dwProcessID);
    }

    if (pThis != NULL)
    {
        pThis->DebugLoop();
    }
    return 0;
}

HRESULT DebugAgent::CreateMinidump(DEBUG_EVENT& debug_event)
{
    HRESULT hr = E_FAIL;

    const auto pDbgDll = ExtensionLibrary::GetLibrary<DbgHelpLibrary>();
    if (pDbgDll == nullptr)
        return E_FAIL;

    API_VERSION dbgVersion = pDbgDll->GetVersion();

    wstring strTempDumpFileName;

    // Open the file
    HANDLE hFile = INVALID_HANDLE_VALUE;
    hr = UtilGetUniquePath(m_strDirectory.c_str(), m_strKeyword.c_str(), strTempDumpFileName, hFile);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(L"Failed CreateFile on '{}' (code: {:#x})", strTempDumpFileName, hr);
        return hr;
    }
    // Create the minidump
    HANDLE hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, debug_event.dwThreadId);
    if (hThread == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(L"Failed OpenThread (tid: {}, code: {:#x})", debug_event.dwThreadId, hr);
        return hr;
    }

    CONTEXT threadContext;
    ZeroMemory(&threadContext, sizeof(CONTEXT));
    GetThreadContext(hThread, &threadContext);
    CloseHandle(hThread);

    MINIDUMP_EXCEPTION_INFORMATION mdei;
    ZeroMemory(&mdei, sizeof(mdei));
    mdei.ThreadId = debug_event.dwThreadId;
    EXCEPTION_POINTERS ExceptionPointers = {&debug_event.u.Exception.ExceptionRecord, &threadContext};
    mdei.ExceptionPointers = &ExceptionPointers;
    mdei.ClientPointers = FALSE;

    MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithHandleData);

    if (dbgVersion.MajorVersion > 6)
    {
        mdt = (MINIDUMP_TYPE)(mdt | MiniDumpWithUnloadedModules);
        switch (dbgVersion.MinorVersion)
        {
            case 1:
                break;
            case 2:
                mdt = (MINIDUMP_TYPE)(mdt | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo | MiniDumpWithCodeSegs);
                break;
            default:
                break;
        }
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, m_dwProcessID);
    if (hProcess == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(
            "Failed to CreateMinidump: OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ) failed (pid: {}, code: "
            "{:#x})",
            m_dwProcessID,
            hr);
        CloseHandle(hFile);
        return hr;
    }

    spdlog::debug(L"Creating Mini Dump %s...", strTempDumpFileName);

    if (FAILED(hr = pDbgDll->MiniDumpWriteDump(hProcess, m_dwProcessID, hFile, mdt, &mdei, nullptr, nullptr)))
    {
        spdlog::error(L"Failed on MiniDumpWriteDump (code: {:#x})", hr);
        return hr;
    }
    else
    {
        spdlog::debug(L"Created Mini Dump '{}'", strTempDumpFileName);
        m_Dumps.push_back(std::move(strTempDumpFileName));
    }

    CloseHandle(hFile);
    CloseHandle(hProcess);

    return S_OK;
}

void CloseHandleHelper(HANDLE h)
{
    if ((h != NULL) && (h != INVALID_HANDLE_VALUE))
    {
        CloseHandle(h);
    }
}

void DebugAgent::DebugLoop()
{
    HRESULT hr = E_FAIL;

    bool bContinue = true;

    spdlog::debug("DebugEvent: Entering event loop for pid: {}", m_dwProcessID);

    do
    {
        DWORD dwExceptionHandled = DBG_CONTINUE;
        ;
        WaitForDebugEvent(&m_Event, INFINITE);

        switch (m_Event.dwDebugEventCode)
        {
            case CREATE_PROCESS_DEBUG_EVENT:
                spdlog::debug("DebugEvent: CREATE_PROCESS_DEBUG_EVENT");
                CloseHandleHelper(m_Event.u.CreateProcessInfo.hFile);
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                spdlog::debug("DebugEvent: CREATE_THREAD_DEBUG_EVENT");
                break;
            case EXCEPTION_DEBUG_EVENT:
                spdlog::debug(
                    "DebugEvent: EXCEPTION_DEBUG_EVENT code: {}, pid: {}, tid: {}, {}",
                    m_Event.u.Exception.ExceptionRecord.ExceptionCode,
                    m_Event.dwProcessId,
                    m_Event.dwThreadId,
                    m_Event.u.Exception.dwFirstChance ? "firstchance" : "lastchance");
                {
                    dwExceptionHandled = DBG_EXCEPTION_NOT_HANDLED;
                    if (!m_Event.u.Exception.dwFirstChance)
                    {
                        spdlog::debug(
                            "Exception occured in child process (code: {:#x})",
                            m_Event.u.Exception.ExceptionRecord.ExceptionCode);
                        if (SUCCEEDED(hr = CreateMinidump(m_Event)))
                        {
                            spdlog::debug(L"Dump file created: '{}'", m_Dumps.back());
                        }
                    }
                    DWORD ExceptionCode = m_Event.u.Exception.ExceptionRecord.ExceptionCode;

                    dwExceptionHandled = DBG_CONTINUE;
                }
                break;
            case EXIT_PROCESS_DEBUG_EVENT:
                spdlog::debug("DebugEvent: EXIT_PROCESS_DEBUG_EVENT");
                bContinue = false;
                break;
            case EXIT_THREAD_DEBUG_EVENT:
                spdlog::debug("DebugEvent: EXIT_THREAD_DEBUG_EVENT");
                break;
            case LOAD_DLL_DEBUG_EVENT:
                spdlog::debug("DebugEvent: LOAD_DLL_DEBUG_EVENT");
                CloseHandleHelper(m_Event.u.LoadDll.hFile);
                break;
            case OUTPUT_DEBUG_STRING_EVENT:
                spdlog::debug("DebugEvent: OUTPUT_DEBUG_STRING_EVENT");
                break;
            case RIP_EVENT:
                spdlog::debug("DebugEvent: RIP_EVENT");
                break;
            case UNLOAD_DLL_DEBUG_EVENT:
                spdlog::debug("DebugEvent: UNLOAD_DLL_DEBUG_EVENT");
                break;
            default:
                spdlog::debug("DebugEvent: Unknown Debug event code: {:#x}", m_Event.dwDebugEventCode);
        }

        // Let the debuggee continue
        if (!ContinueDebugEvent(m_Event.dwProcessId, m_Event.dwThreadId, dwExceptionHandled))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            spdlog::error("Failed to ContinueDebugEvent() (code: {:#x})", hr);
            return;
        }
    } while (bContinue);
    spdlog::debug("DebugEvent: Exiting Debug Loop");
}

std::shared_ptr<DebugAgent>
DebugAgent::DebugProcess(DWORD dwProcessID, const std::wstring& strDirectory, const std::wstring& strKeyword)
{
    auto pAgent = std::shared_ptr<DebugAgent>(new DebugAgent(strDirectory, strKeyword));
    if (pAgent == nullptr)
        return pAgent;

    pAgent->m_dwProcessID = dwProcessID;

    if (!QueueUserWorkItem(
            (LPTHREAD_START_ROUTINE)DebugAgent::StaticDebugLoopProc, pAgent.get(), WT_EXECUTELONGFUNCTION))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        spdlog::error(L"Failed to queue debugger agent (code: {:#x})", hr);
        return nullptr;
    }
    return pAgent;
}

DebugAgent::~DebugAgent(void) {}
