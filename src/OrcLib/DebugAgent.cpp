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

#include "LogFileWriter.h"

#include "DbgHelpLibrary.h"

using namespace std;

using namespace Orc;

DebugAgent::DebugAgent(logger pLog, const std::wstring& strDirectory, const std::wstring& strKeyword)
    : _L_(std::move(pLog))
    , m_strDirectory(strDirectory)
    , m_strKeyword(strKeyword)
{
    ZeroMemory(&m_Event, sizeof(DEBUG_EVENT));
    m_dbghelp = ExtensionLibrary::GetLibrary<DbgHelpLibrary>(_L_);
}

DWORD WINAPI DebugAgent::StaticDebugLoopProc(__in LPVOID lpParameter)
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<DebugAgent> pThis = ((DebugAgent*)lpParameter)->shared_from_this();

    if (!DebugActiveProcess(pThis->m_dwProcessID))
    {
        log::Error(
            pThis->_L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to attach debugger to running process (pid = %d)\r\n",
            pThis->m_dwProcessID);
        return hr;
    }
    else
    {
        log::Verbose(pThis->_L_, L"Attached debugger to running process (pid = %d)\r\n", pThis->m_dwProcessID);
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

    const auto pDbgDll = ExtensionLibrary::GetLibrary<DbgHelpLibrary>(_L_);
    if (pDbgDll == nullptr)
        return E_FAIL;

    API_VERSION dbgVersion = pDbgDll->GetVersion();

    wstring strTempDumpFileName;

    // Open the file
    HANDLE hFile = INVALID_HANDLE_VALUE;
    hr = UtilGetUniquePath(_L_, m_strDirectory.c_str(), m_strKeyword.c_str(), strTempDumpFileName, hFile);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"CreateFile failed. (file=%s)\r\n",
            strTempDumpFileName.c_str());
        return hr;
    }
    // Create the minidump
    HANDLE hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, debug_event.dwThreadId);
    if (hThread == NULL)
    {
        log::Error(
            _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"OpenThread failed. (tid=%u)\r\n", debug_event.dwThreadId);
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
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"CreateMinidump: OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ) failed. (pid=%u)\r\n", m_dwProcessID);
        // Close the file
        CloseHandle(hFile);
        return hr;
    }

    log::Verbose(_L_, L"Creating Mini Dump %s...\r\n", strTempDumpFileName.c_str());

    if (FAILED(hr = pDbgDll->MiniDumpWriteDump(hProcess, m_dwProcessID, hFile, mdt, &mdei, nullptr, nullptr)))
    {
        log::Error(_L_, hr, L"MiniDumpWriteDump failed\r\n");
        return hr;
    }
    else
    {
        log::Verbose(_L_, L"Created Mini Dump %s\r\n", strTempDumpFileName.c_str());
        m_Dumps.push_back(std::move(strTempDumpFileName));
    }

    // Close the file
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

    log::Verbose(_L_, L"DebugEvent: Entering event loop for process %d\r\n", m_dwProcessID);

    do
    {
        DWORD dwExceptionHandled = DBG_CONTINUE;
        ;
        WaitForDebugEvent(&m_Event, INFINITE);

        switch (m_Event.dwDebugEventCode)
        {
            case CREATE_PROCESS_DEBUG_EVENT:
                log::Verbose(_L_, L"DebugEvent: CREATE_PROCESS_DEBUG_EVENT\r\n");
                CloseHandleHelper(m_Event.u.CreateProcessInfo.hFile);
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                log::Verbose(_L_, L"DebugEvent: CREATE_THREAD_DEBUG_EVENT\r\n");
                break;
            case EXCEPTION_DEBUG_EVENT:
                log::Verbose(
                    _L_,
                    L"DebugEvent: EXCEPTION_DEBUG_EVENT Code=0x%lx, pid=%d, tid=%d, %s\r\n",
                    m_Event.u.Exception.ExceptionRecord.ExceptionCode,
                    m_Event.dwProcessId,
                    m_Event.dwThreadId,
                    m_Event.u.Exception.dwFirstChance ? L"firstchance" : L"lastchance");
                {
                    dwExceptionHandled = DBG_EXCEPTION_NOT_HANDLED;
                    if (!m_Event.u.Exception.dwFirstChance)
                    {
                        log::Verbose(
                            _L_,
                            L"Exception occured in child process (code=0x%lx)\r\n",
                            m_Event.u.Exception.ExceptionRecord.ExceptionCode);
                        if (SUCCEEDED(hr = CreateMinidump(m_Event)))
                        {
                            log::Verbose(_L_, L"Dump file created (%s)\r\n", m_Dumps.back().c_str());
                        }
                    }
                    DWORD ExceptionCode = m_Event.u.Exception.ExceptionRecord.ExceptionCode;

                    dwExceptionHandled = DBG_CONTINUE;
                }
                break;
            case EXIT_PROCESS_DEBUG_EVENT:
                log::Verbose(_L_, L"DebugEvent: EXIT_PROCESS_DEBUG_EVENT\r\n");
                bContinue = false;
                break;
            case EXIT_THREAD_DEBUG_EVENT:
                log::Verbose(_L_, L"DebugEvent: EXIT_THREAD_DEBUG_EVENT\r\n");
                break;
            case LOAD_DLL_DEBUG_EVENT:
                log::Verbose(_L_, L"DebugEvent: LOAD_DLL_DEBUG_EVENT\r\n");
                CloseHandleHelper(m_Event.u.LoadDll.hFile);
                break;
            case OUTPUT_DEBUG_STRING_EVENT:
                log::Verbose(_L_, L"DebugEvent: OUTPUT_DEBUG_STRING_EVENT\r\n");
                break;
            case RIP_EVENT:
                log::Verbose(_L_, L"DebugEvent: RIP_EVENT\r\n");
                break;
            case UNLOAD_DLL_DEBUG_EVENT:
                log::Verbose(_L_, L"DebugEvent: UNLOAD_DLL_DEBUG_EVENT\r\n");
                break;
            default:
                log::Verbose(_L_, L"DebugEvent: Unknown Debug event code: %d\r\n", m_Event.dwDebugEventCode);
        }

        // Let the debuggee continue
        if (!ContinueDebugEvent(m_Event.dwProcessId, m_Event.dwThreadId, dwExceptionHandled))
        {
            log::Error(_L_, HRESULT_FROM_WIN32(GetLastError()), L"Failed to ContinueDebugEvent()\r\n");
            return;
        }
    } while (bContinue);
    log::Verbose(_L_, L"DebugEvent: Exiting Debug Loop\r\n");
}

std::shared_ptr<DebugAgent> DebugAgent::DebugProcess(
    const logger& pLog,
    DWORD dwProcessID,
    const std::wstring& strDirectory,
    const std::wstring& strKeyword)
{
    auto pAgent = std::make_shared<DebugAgent>(pLog, strDirectory, strKeyword);
    if (pAgent == nullptr)
        return pAgent;

    pAgent->m_dwProcessID = dwProcessID;

    if (!QueueUserWorkItem(
            (LPTHREAD_START_ROUTINE)DebugAgent::StaticDebugLoopProc, pAgent.get(), WT_EXECUTELONGFUNCTION))
    {
        log::Error(pLog, HRESULT_FROM_WIN32(GetLastError()), L"Failed to queue debugger agent\r\n");
        return nullptr;
    }
    return pAgent;
}

DebugAgent::~DebugAgent(void) {}
