//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "Robustness.h"

#include "CriticalSection.h"

using namespace Orc;

namespace {

BOOL MiniDumpWriteDumpApi(
    HANDLE hProcess,
    DWORD dwPid,
    HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam,
    std::error_code& ec)
{
    using FnMiniDumpWriteDump = BOOL(__stdcall*)(
        HANDLE hProcess,
        DWORD dwPid,
        HANDLE hFile,
        MINIDUMP_TYPE DumpType,
        PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
        PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
        PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

    static FnMiniDumpWriteDump fn = nullptr;

    if (fn == nullptr)
    {
        static HMODULE module = NULL;

        if (!module)
        {
            module = GetModuleHandleA("Dbghelp.dll");
            if (module == NULL)
            {
                ec.assign(::GetLastError(), std::system_category());
                return FALSE;
            }
        }

        fn = reinterpret_cast<FnMiniDumpWriteDump>(::GetProcAddress(module, "MiniDumpWriteDump"));
        if (!fn)
        {
            ec.assign(::GetLastError(), std::system_category());
            return FALSE;
        }
    }

    BOOL ret = fn(hProcess, dwPid, hFile, DumpType, ExceptionParam, UserStreamParam, CallbackParam);
    if (!ret)
    {
        ec.assign(::GetLastError(), std::system_category());
        return FALSE;
    }

    return TRUE;
}

}  // namespace

struct TerminationBlock
{
    std::vector<std::shared_ptr<TerminationHandler>> _Handlers;
    Orc::CriticalSection _CritSect;

    const WCHAR* _szProcessDescr = nullptr;
    bool _bSilent = false;
    bool _TerminateCalled = false;
    bool _bWriteMinidumpFromCurrentProcess = false;
    HANDLE _hTerminationHandlers = INVALID_HANDLE_VALUE;

    void Terminate();

    ~TerminationBlock() { Terminate(); }
};

void TerminationBlock::Terminate()
{
    if (_TerminateCalled)
        return;

    ResetEvent(_hTerminationHandlers);

    std::vector<std::shared_ptr<TerminationHandler>> HandlersCopy;
    {
        ScopedLock sl(_CritSect);
        std::swap(_Handlers, HandlersCopy);
    }
    auto silent_handler = [](const std::shared_ptr<TerminationHandler>& pHandler) {
        try
        {
            (*pHandler)();
        }
        catch (...)
        {
        }
    };
    auto handler_nodescr = [](const std::shared_ptr<TerminationHandler>& pHandler) {
        try
        {
            HRESULT hr = E_FAIL;
            if (FAILED(hr = (*pHandler)()))
            {
                wprintf(L"\"%s\" finalizer failed with code hr=0x%lx\n", pHandler->Description.c_str(), hr);
            }
        }
        catch (...)
        {
            wprintf(L"\"%s\" finalizer failed by throwing exception\n", pHandler->Description.c_str());
        }
    };
    auto handler_withdescr = [this](const std::shared_ptr<TerminationHandler>& pHandler) {
        try
        {
            HRESULT hr = E_FAIL;

            if (FAILED(hr = (*pHandler)()))
            {
                wprintf(
                    L"%s: \"%s\" finalizer failed with code hr=0x%lx\n",
                    _szProcessDescr,
                    pHandler->Description.c_str(),
                    hr);
            }
        }
        catch (...)
        {
            wprintf(
                L"%s: \"%s\" finalizer failed by throwing exception\n", _szProcessDescr, pHandler->Description.c_str());
        }
    };

    if (_bSilent)
        std::for_each(HandlersCopy.begin(), HandlersCopy.end(), silent_handler);
    else
    {
        if (_szProcessDescr)
            std::for_each(HandlersCopy.begin(), HandlersCopy.end(), handler_withdescr);
        else
            std::for_each(HandlersCopy.begin(), HandlersCopy.end(), handler_nodescr);
    }
    if (!_bSilent)
        wprintf(L"%s: All handlers have been called\n", _szProcessDescr);

    SetEvent(_hTerminationHandlers);
    _TerminateCalled = true;
    return;
}

struct TerminationBlockGuard
{
    std::unique_ptr<TerminationBlock> block;

    ~TerminationBlockGuard()
    {
        if (block)
        {
            block->_bSilent = true;
            block->Terminate();
            block = nullptr;
        }
    }

    const std::unique_ptr<TerminationBlock>& Block() { return block; }

    explicit operator bool()
    {
        if (block)
            return !block->_TerminateCalled;
        return false;
    }
};

TerminationBlockGuard g_termination;

int Robustness::handle_program_memory_depletion(size_t attempted)
{
    if (!g_termination)
        throw MemoryException(attempted);

    if (!g_termination.Block()->_bSilent)
    {
        char buffer[1024];

        if (g_termination.block->_szProcessDescr)
        {
            snprintf(
                buffer,
                sizeof(buffer) - 1,
                "%S: ERROR: failed to allocate %zu bytes",
                g_termination.Block()->_szProcessDescr,
                attempted);
        }
        else
        {
            snprintf(buffer, sizeof(buffer) - 1, "ERROR: failed to allocate %zu bytes", attempted);
        }

        buffer[sizeof(buffer) - 1] = '\0';
        std::cerr << buffer << std::endl;
        Log::Critical(buffer);

        MEMORYSTATUSEX memory;
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory))
        {
            const auto div = 1048576;

            snprintf(
                buffer,
                sizeof(buffer) - 1,
                "Memory: physical: %I64u/%I64u MB, paged: %I64u/%I64u MB, virtual: %I64u/%I64u MB\n",
                memory.ullAvailPhys / div,
                memory.ullTotalPhys / div,
                memory.ullAvailPageFile / div,
                memory.ullTotalPageFile / div,
                memory.ullAvailVirtual / div,
                memory.ullTotalVirtual / div);
        }

        std::cerr << buffer << std::endl;

        auto logger = Log::DefaultLogger();
        if (logger)
        {
            logger->Critical("Failed to allocate buffer");
            logger->Critical(buffer);
        }
    }

    throw MemoryException(attempted);
}

void Robustness::seTransFunction(unsigned int u, EXCEPTION_POINTERS* pExp)
{
    if (!g_termination)
        throw SystemException(u, pExp);

    if (!g_termination.Block()->_bSilent)
    {
        if (g_termination.Block()->_szProcessDescr)
        {
            wprintf(L"\n%s: ERROR: System exception: 0x%lx\n", g_termination.Block()->_szProcessDescr, u);
            Log::Critical(L"{}: System exception: {}", g_termination.Block()->_szProcessDescr, SystemError(u));
        }
        else
        {
            wprintf(L"\nERROR: System exception: 0x%lx\n", u);
            Log::Critical("System exception: {}", SystemError(u));
        }

        assert(0 && "System exception");
    }

    throw SystemException(u, pExp);
}

void Robustness::Terminate()
{
    Terminate(false);
}

void Robustness::Terminate(bool bSilent)
{
    if (!g_termination)
        return;

    g_termination.Block()->_bSilent = bSilent;
    g_termination.Block()->Terminate();

    return;
}

LONG WINAPI Robustness::UnhandledExceptionFilter(__in struct _EXCEPTION_POINTERS* ExceptionInfo)
{
    if (!g_termination)
        throw SystemException(ExceptionInfo->ExceptionRecord->ExceptionCode, ExceptionInfo);

    if (!g_termination.Block()->_bSilent)
    {
        if (g_termination.Block()->_szProcessDescr)
        {
            wprintf(
                L"\n%s: ERROR: UnHandled Exception (code: 0x%lx, address: 0x%llx)\n",
                g_termination.Block()->_szProcessDescr,
                ExceptionInfo->ExceptionRecord->ExceptionCode,
                (ULONGLONG)ExceptionInfo->ExceptionRecord->ExceptionAddress);
        }
        else
        {
            wprintf(
                L"\nERROR:UnHandled Exception (code: 0x%lx, address: 0x%llx)\n",
                ExceptionInfo->ExceptionRecord->ExceptionCode,
                (ULONGLONG)ExceptionInfo->ExceptionRecord->ExceptionAddress);
        }
    }

    if (g_termination.Block()->_bWriteMinidumpFromCurrentProcess)
    {
        const char filename[] = "dfir-orc.dmp";

        HANDLE hFile = ::CreateFileA(
            filename,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hFile != INVALID_HANDLE_VALUE)
        {
            std::error_code ec;

            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
            exceptionInfo.ThreadId = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = ExceptionInfo;
            exceptionInfo.ClientPointers = TRUE;

            ::MiniDumpWriteDumpApi(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                hFile,
                MINIDUMP_TYPE::MiniDumpNormal,
                &exceptionInfo,
                NULL,
                NULL,
                ec);
            if (ec)
            {
                printf("Failed MiniDumpWriteDumpApi (code: %x)\n", ec.value());
            }
            else
            {
                wchar_t currentDirectory[MAX_PATH + 1];
                currentDirectory[MAX_PATH] = L'\0';
                GetCurrentDirectoryW(MAX_PATH, currentDirectory);
                printf("MiniDumpWriteDump: write %S%s\n", currentDirectory, filename);
            }

            CloseHandle(hFile);
        }
    }

    Terminate();
    if (!IsDebuggerPresent())
        exit(-1);
    else
        throw SystemException(ExceptionInfo->ExceptionRecord->ExceptionCode, ExceptionInfo);
}

BOOL WINAPI Robustness::ConsoleEventHandlerRoutine(__in DWORD dwCtrlType)
{
    if (!g_termination)
    {
        if (!IsDebuggerPresent())
            ExitProcess((UINT)-1);
        else
            throw ConsoleException(dwCtrlType);
    }

    const WCHAR* szEvent = nullptr;

    switch (dwCtrlType)
    {
        case CTRL_C_EVENT:
            szEvent = L"A CTRL+C signal was received";
            break;
        case CTRL_BREAK_EVENT:
            szEvent = L"A CTRL+BREAK signal was received";
            break;
        case CTRL_CLOSE_EVENT:
            szEvent = L"A Close signal was received";
            break;
        case CTRL_LOGOFF_EVENT:
            szEvent = L"INFO: A logoff signal was received: \"One\" user logged off\n";
            wprintf(szEvent);
            OutputDebugString(szEvent);
            return FALSE;
        case CTRL_SHUTDOWN_EVENT:
            szEvent = L"A shutdown signal was received";
            break;
    }

    if (!g_termination.Block()->_bSilent)
    {
        if (g_termination.Block()->_szProcessDescr)
            wprintf(L"\n%s: ERROR: %s\n", g_termination.Block()->_szProcessDescr, szEvent);
        else
            wprintf(L"\nERROR: %s\n", szEvent);
    }

    Robustness::Terminate();
    if (!IsDebuggerPresent())
        ExitProcess((UINT)-1);
    else
        throw ConsoleException(dwCtrlType);
}

void Robustness::AddTerminationHandler(const std::shared_ptr<TerminationHandler>& pHandler)
{
    if (!g_termination)
        return;

    ScopedLock sl(g_termination.Block()->_CritSect);

    if (pHandler != nullptr)
        g_termination.Block()->_Handlers.push_back(pHandler);

    std::stable_sort(
        g_termination.Block()->_Handlers.begin(),
        g_termination.Block()->_Handlers.end(),
        [](const std::shared_ptr<TerminationHandler>& pLeft,
           const std::shared_ptr<TerminationHandler>& pRigth) -> bool { return pLeft->Priority > pRigth->Priority; });
}

void Robustness::RemoveTerminationHandler(const std::shared_ptr<TerminationHandler>& pHandler)
{
    if (!g_termination)
        return;

    ScopedLock sl(g_termination.Block()->_CritSect);

    auto newend = std::remove_if(
        begin(g_termination.Block()->_Handlers),
        end(g_termination.Block()->_Handlers),
        [pHandler](const std::shared_ptr<TerminationHandler>& item) -> bool { return item == pHandler; });
    if (newend != end(g_termination.Block()->_Handlers))
        g_termination.Block()->_Handlers.erase(newend, end(g_termination.Block()->_Handlers));
    return;
}

void Robustness::EnableMinidumpFromCurrentProcess(BOOL value)
{
    if (!g_termination)
        return;

    g_termination.Block()->_bWriteMinidumpFromCurrentProcess = value;
}

HRESULT Robustness::Initialize(const WCHAR* szProcessDescr, bool bSilent, bool bOverrideGobalHandlers)
{
    g_termination.block = std::make_unique<TerminationBlock>();
    if (!g_termination)
        return E_OUTOFMEMORY;

    g_termination.Block()->_szProcessDescr = szProcessDescr;
    g_termination.Block()->_bSilent = bSilent;

    if (bOverrideGobalHandlers)
    {
        SetErrorMode(SEM_FAILCRITICALERRORS);

        _set_new_mode(1);
        _set_new_handler(handle_program_memory_depletion);

        _set_se_translator(Robustness::seTransFunction);

        set_terminate(Robustness::Terminate);

        SetUnhandledExceptionFilter(Robustness::UnhandledExceptionFilter);

        SetConsoleCtrlHandler(Robustness::ConsoleEventHandlerRoutine, TRUE);
    }

    g_termination.Block()->_hTerminationHandlers = CreateEvent(NULL, TRUE, TRUE, NULL);

    if (g_termination.Block()->_hTerminationHandlers == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

HRESULT Robustness::UnInitialize(DWORD dwTimeout)
{
    if (WAIT_TIMEOUT == WaitForSingleObject(g_termination.Block()->_hTerminationHandlers, dwTimeout))
    {
        wprintf(L"\nTermination handlers timed out!!\n");
    }
    return S_OK;
}
