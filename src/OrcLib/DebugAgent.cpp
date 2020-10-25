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

#include "Log/Log.h"

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
        Log::Error(
            "Failed to attach debugger to running process (pid: {}, [{}])", pThis->m_dwProcessID, SystemError(hr));
        return hr;
    }
    else
    {
        Log::Debug("Attached debugger to running process (pid: {})", pThis->m_dwProcessID);
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
        Log::Error(L"Failed CreateFile on '{}' [{}]", strTempDumpFileName, SystemError(hr));
        return hr;
    }
    // Create the minidump
    HANDLE hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, debug_event.dwThreadId);
    if (hThread == NULL)
    {
        const auto ec = LastWin32Error();
        Log::Error(L"Failed OpenThread (tid: {}, [{}])", debug_event.dwThreadId, ec
        );
        return ToHRESULT(ec);
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
        Log::Error(
            "Failed to CreateMinidump: OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ) failed (pid: {}, [{}])",
            m_dwProcessID,
            SystemError(hr));
        CloseHandle(hFile);
        return hr;
    }

    Log::Debug(L"Creating Mini Dump {}...", strTempDumpFileName);

    if (FAILED(hr = pDbgDll->MiniDumpWriteDump(hProcess, m_dwProcessID, hFile, mdt, &mdei, nullptr, nullptr)))
    {
        Log::Error(L"Failed on MiniDumpWriteDump [{}]", SystemError(hr));
        return hr;
    }
    else
    {
        Log::Debug(L"Created Mini Dump '{}'", strTempDumpFileName);
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

    Log::Debug("DebugEvent: Entering event loop for pid: {}", m_dwProcessID);

    do
    {
        DWORD dwExceptionHandled = DBG_CONTINUE;
        ;
        WaitForDebugEvent(&m_Event, INFINITE);

        switch (m_Event.dwDebugEventCode)
        {
            case CREATE_PROCESS_DEBUG_EVENT:
                Log::Debug("DebugEvent: CREATE_PROCESS_DEBUG_EVENT");
                CloseHandleHelper(m_Event.u.CreateProcessInfo.hFile);
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                Log::Debug("DebugEvent: CREATE_THREAD_DEBUG_EVENT");
                break;
            case EXCEPTION_DEBUG_EVENT:
                Log::Debug(
                    "DebugEvent: EXCEPTION_DEBUG_EVENT code: {}, pid: {}, tid: {}, {}",
                    m_Event.u.Exception.ExceptionRecord.ExceptionCode,
                    m_Event.dwProcessId,
                    m_Event.dwThreadId,
                    m_Event.u.Exception.dwFirstChance ? "firstchance" : "lastchance");
                {
                    dwExceptionHandled = DBG_EXCEPTION_NOT_HANDLED;
                    if (!m_Event.u.Exception.dwFirstChance)
                    {
                        Log::Debug(
                            "Exception occured in child process [{}]",
                            SystemError(m_Event.u.Exception.ExceptionRecord.ExceptionCode));
                        if (SUCCEEDED(hr = CreateMinidump(m_Event)))
                        {
                            Log::Debug(L"Dump file created: '{}'", m_Dumps.back());
                        }
                    }
                    DWORD ExceptionCode = m_Event.u.Exception.ExceptionRecord.ExceptionCode;

                    dwExceptionHandled = DBG_CONTINUE;
                }
                break;
            case EXIT_PROCESS_DEBUG_EVENT:
                Log::Debug("DebugEvent: EXIT_PROCESS_DEBUG_EVENT");
                bContinue = false;
                break;
            case EXIT_THREAD_DEBUG_EVENT:
                Log::Debug("DebugEvent: EXIT_THREAD_DEBUG_EVENT");
                break;
            case LOAD_DLL_DEBUG_EVENT:
                Log::Debug("DebugEvent: LOAD_DLL_DEBUG_EVENT");
                CloseHandleHelper(m_Event.u.LoadDll.hFile);
                break;
            case OUTPUT_DEBUG_STRING_EVENT:
                Log::Debug("DebugEvent: OUTPUT_DEBUG_STRING_EVENT");
                break;
            case RIP_EVENT:
                Log::Debug("DebugEvent: RIP_EVENT");
                break;
            case UNLOAD_DLL_DEBUG_EVENT:
                Log::Debug("DebugEvent: UNLOAD_DLL_DEBUG_EVENT");
                break;
            default:
                Log::Debug("DebugEvent: Unknown Debug event code: {}", m_Event.dwDebugEventCode);
        }

        // Let the debuggee continue
        if (!ContinueDebugEvent(m_Event.dwProcessId, m_Event.dwThreadId, dwExceptionHandled))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error("Failed to ContinueDebugEvent() [{}]", SystemError(hr));
            return;
        }
    } while (bContinue);
    Log::Debug("DebugEvent: Exiting Debug Loop");
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
        Log::Error(L"Failed to queue debugger agent [{}]", SystemError(hr));
        return nullptr;
    }
    return pAgent;
}

DebugAgent::~DebugAgent(void) {}
