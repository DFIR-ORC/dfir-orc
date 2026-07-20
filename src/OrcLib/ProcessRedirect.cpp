//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <memory>
#include <agents.h>

#include "ProcessRedirect.h"

#include "CommandExecute.h"

#include "ByteStream.h"

#include "Log/Log.h"
#include "Utils/WinApi.h"
#include "SystemDetails.h"
#include "Kernel32Extension.h"

using namespace std;
using namespace Concurrency;

using namespace Orc;

ProcessRedirect::ProcessRedirect(ProcessInOut selection)
    : m_Status(Initialized)
    , m_Select(selection)
    , m_pBS(NULL)
    , m_bCloseStream(false)
    , m_ReadHandle(INVALID_HANDLE_VALUE)
    , m_WriteHandle(INVALID_HANDLE_VALUE)
    , m_DuplicateHandle(INVALID_HANDLE_VALUE)
{
    ZeroMemory(&m_ASyncIO, sizeof(PROCESS_REDIRECT));
    m_ASyncIO.pThis = this;

    m_hIoDrained = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (m_hIoDrained == NULL)
    {
        // Teardown then cannot wait for an in flight completion: keep going, but say so.
        Log::Warn(L"Failed to create redirection drain event [{}]", Win32Error(GetLastError()));
    }
}

struct ProcessRedirect_make_shared_enabler : public Orc::ProcessRedirect
{
    inline ProcessRedirect_make_shared_enabler(ProcessInOut selection)
        : Orc::ProcessRedirect(selection) {};
};

std::shared_ptr<ProcessRedirect>
ProcessRedirect::MakeRedirect(ProcessInOut selection, std::shared_ptr<ByteStream> pBS, bool bClose)
{
    auto retval = make_shared<ProcessRedirect_make_shared_enabler>(selection);

    retval->m_pBS = pBS;
    retval->m_bCloseStream = bClose;

    return retval;
}

HRESULT ProcessRedirect::SubmitRead()
{
    DWORD dwLastError = ERROR_SUCCESS;

    {
        Concurrency::critical_section::scoped_lock lock(m_IoCs);

        if (m_bClosing || m_ReadHandle == INVALID_HANDLE_VALUE)
        {
            return S_OK;
        }

        // Arming the read under the lock keeps DrainPendingIo() from cancelling a read that is not
        // submitted yet, and keeps the count it observes final.
        if (m_PendingIo == 0 && m_hIoDrained != NULL)
        {
            ResetEvent(m_hIoDrained);
        }

        m_PendingIo++;

        DWORD dwBytesRead = 0L;
        if (!ReadFile(m_ReadHandle, m_ASyncIO.Buffer, BUFFER_SIZE, &dwBytesRead, &m_ASyncIO.OL))
        {
            dwLastError = GetLastError();
        }

        // The handle is bound to the thread pool, so a completion is queued on synchronous success too. A
        // synchronous failure queues nothing: that read never reaches OnIoCompleted().
        if (dwLastError != ERROR_SUCCESS && dwLastError != ERROR_IO_PENDING && dwLastError != ERROR_MORE_DATA)
        {
            m_PendingIo--;
            if (m_PendingIo == 0 && m_hIoDrained != NULL)
            {
                SetEvent(m_hIoDrained);
            }
        }
    }

    switch (dwLastError)
    {
        case ERROR_SUCCESS:
        case ERROR_IO_PENDING:
        case ERROR_MORE_DATA:
            SetStatus(PendingIO);
            return S_OK;
        case ERROR_BROKEN_PIPE:
            SetStatus(Complete);
            Close();
            return S_OK;
        default:
            Log::Error(L"ReadFile from child failed [{}]", Win32Error(dwLastError));
            return HRESULT_FROM_WIN32(dwLastError);
    }
}

void ProcessRedirect::DrainPendingIo()
{
    HANDLE hReadHandle = INVALID_HANDLE_VALUE;
    bool bPending = false;

    {
        Concurrency::critical_section::scoped_lock lock(m_IoCs);

        m_bClosing = true;
        bPending = m_PendingIo > 0;

        // Dropping the handle under the same lock SubmitRead() uses guarantees no read is armed past here.
        hReadHandle = m_ReadHandle;
        m_ReadHandle = INVALID_HANDLE_VALUE;
    }

    if (hReadHandle != INVALID_HANDLE_VALUE)
    {
        const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>();
        if (pk32)
        {
            pk32->CancelIoEx(hReadHandle, &m_ASyncIO.OL);
        }

        // Closing forces any read the cancel missed to complete with ERROR_OPERATION_ABORTED, so the wait
        // below always ends: on XP the cancel degrades to CancelIo, which only cancels this thread's reads.
        CloseHandle(hReadHandle);
    }

    // The completion callback cannot wait for itself. It re-arms nothing once m_bClosing is set, so the
    // caller that owns this object drains it instead.
    if (!bPending || m_hIoDrained == NULL || m_CallbackThreadId == GetCurrentThreadId())
    {
        return;
    }

    // The aborted completion runs on a thread pool thread and must not outlive this object.
    WaitForSingleObject(m_hIoDrained, INFINITE);
}

void ProcessRedirect::OnIoCompleted(__in DWORD dwErrorCode, __in DWORD dwNumberOfBytesTransfered)
{
    m_CallbackThreadId = GetCurrentThreadId();

    ProcessIncomingData(dwErrorCode, dwNumberOfBytesTransfered);

    m_CallbackThreadId = 0;

    bool bDrained = false;
    {
        Concurrency::critical_section::scoped_lock lock(m_IoCs);
        m_PendingIo--;
        bDrained = m_PendingIo == 0;
    }

    if (!bDrained)
    {
        return;
    }

    HANDLE hIoDrained = m_hIoDrained;
    if (hIoDrained != NULL)
    {
        // Last touch on this object: DrainPendingIo() may return and its owner destroy us right after.
        SetEvent(hIoDrained);
    }
}


HANDLE ProcessRedirect::GetChildHandleFor(ProcessInOut selection)
{
    if ((selection & StdInput) && (m_Select & StdInput))
        return m_ReadHandle;
    if ((selection & StdOutput) && (m_Select & StdOutput))
        return m_WriteHandle;
    if ((selection & StdError) && (m_Select & StdError))
        return m_WriteHandle;
    if ((selection & StdError) && (m_Select & (StdOutput | StdError)))
        return m_DuplicateHandle;
    return INVALID_HANDLE_VALUE;
}

HANDLE ProcessRedirect::GetParentHandleFor(ProcessInOut selection)
{
    if ((selection & StdInput) && (m_Select & StdInput))
        return m_WriteHandle;
    if ((selection & StdOutput) && (m_Select & StdOutput))
        return m_ReadHandle;
    if ((selection & StdError) && (m_Select & (StdOutput | StdError)))
        return m_ReadHandle;
    if ((selection & StdError) && (m_Select & StdError))
        return m_ReadHandle;
    return INVALID_HANDLE_VALUE;
}

HRESULT ProcessRedirect::ProcessIncomingData(__in DWORD dwErrorCode, __in DWORD dwNumberOfBytesTransfered)
{
    if (ERROR_SUCCESS == dwErrorCode && m_pBS != NULL)
    {
        ULONGLONG ullBytesWritten = 0LL;
        HRESULT hr = m_pBS->Write(m_ASyncIO.Buffer, dwNumberOfBytesTransfered, &ullBytesWritten);
        if (FAILED(hr))
        {
            Log::Warn(L"Failed to write incoming data [{}]", SystemError(hr));
        }
    }

    SubmitRead();
    return S_OK;
}

VOID ProcessRedirect::FileIOCompletionRoutine(
    __in DWORD dwErrorCode,
    __in DWORD dwNumberOfBytesTransfered,
    __in LPOVERLAPPED lpOverlapped)
{
    PROCESS_REDIRECT* pASyncIO = (PROCESS_REDIRECT*)lpOverlapped;

    pASyncIO->pThis->OnIoCompleted(dwErrorCode, dwNumberOfBytesTransfered);
    return;
}

void ProcessRedirect::SetStatus(RedirectStatus status)
{
    Concurrency::critical_section::scoped_lock lock(m_cs);
    if (status > m_Status)
        m_Status = status;
}

HRESULT ProcessRedirect::CreatePipe(const WCHAR* szUniqueSuffix)
{
    if (m_Select & StdInput && (m_Select & StdOutput || m_Select & StdError))
        return E_INVALIDARG;

    auto sd = GetSecurityDescriptor(DACL::OrcDefaultFullControl);
    if (!sd)
    {
        Log::Debug(L"Failed to get security descriptor [{}]", sd.error());
        return ToHRESULT(sd.error());
    }

    // you need this for the client to inherit the handles	SECURITY_ATTRIBUTES sa;
    SECURITY_ATTRIBUTES sa;
    // Set up the security attributes struct.
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;  // this is the critical bit
    sa.lpSecurityDescriptor = sd.value();

    //
    // this creates a inheritable, one-way handle for the server to read
    //
    HANDLE hTmpHandle = INVALID_HANDLE_VALUE;

    std::wstring pipeName;

    DWORD osMajor = 0;
    DWORD osMinor = 0;
    DWORD pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE;
    if (SUCCEEDED(SystemDetails::GetOSVersion(osMajor, osMinor)) && osMajor >= 6)
    {
        // PIPE_REJECT_REMOTE_CLIENTS is Vista+ (OS major >= 6); keeps these local stdio pipes off SMB, skip on XP.
        pipeMode |= PIPE_REJECT_REMOTE_CLIENTS;
    }

    if ((m_Select & StdOutput) || (m_Select & StdError))
    {
        if ((m_Select & StdOutput) && (m_Select & StdError))
        {
            pipeName = fmt::format(L"\\\\.\\pipe\\DFIR-ORC_{}_out_err_{}", GetCurrentProcessId(), szUniqueSuffix);
        }
        else if (m_Select & StdOutput)
        {
            pipeName = fmt::format(L"\\\\.\\pipe\\DFIR-ORC_{}_output_{}", GetCurrentProcessId(), szUniqueSuffix);
        }
        else if (m_Select & StdError)
        {
            pipeName = fmt::format(L"\\\\.\\pipe\\DFIR-ORC_{}_error_{}", GetCurrentProcessId(), szUniqueSuffix);
        }

        if ((hTmpHandle = CreateNamedPipe(
                 pipeName.c_str(),
                 PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                 pipeMode,
                 PIPE_UNLIMITED_INSTANCES,
                 BUFFER_SIZE,
                 BUFFER_SIZE,
                 0,
                 &sa))
            == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());
        //
        // however, the client can not use an inbound server handle.  Client needs a write-only,
        // outgoing handle.
        // So, you create another handle to the same named pipe, only write-only.
        // Again, must be created with the inheritable attribute, and the options are important.
        //
        // use CreateFile to open a new handle to the existing pipe...
        //
        auto hWrite = CreateFileApi(
            pipeName.c_str(),
            FILE_WRITE_DATA | SYNCHRONIZE,
            0,
            &sa,
            OPEN_EXISTING,  // very important flag!
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
            0  // no template file for OPEN_EXISTING
        );

        if (!hWrite)
        {
            return ToHRESULT(hWrite.error());
        }

        m_WriteHandle = hWrite->release();

        // we're going to give the same pipe for stderr, so duplicate for that:
        // Create a duplicate of the output write handle for the std error
        // write handle. This is necessary in case the child application
        //// closes one of its std output handles.

        if ((m_Select & StdOutput) && (m_Select & StdError))
        {
            if (!::DuplicateHandle(
                    GetCurrentProcess(),
                    m_WriteHandle,
                    GetCurrentProcess(),
                    &m_DuplicateHandle,
                    0,
                    TRUE,
                    DUPLICATE_SAME_ACCESS))
                return HRESULT_FROM_WIN32(GetLastError());
        }

        // All is well?  not quite.  Our main server-side handle was created shareable.
        // That means the client will receive it, and we have a problem because
        // pipe termination conditions rely on knowing when the last handle closes.
        // So, the only answer is to create another one, just for the server...
        // Create new output read handle and the input write handles. Set
        // the Inheritance property to FALSE. Otherwise, the child inherits the
        // properties and, as a result, non-closeable handles to the pipes
        // are created.
        if (!::DuplicateHandle(
                GetCurrentProcess(),
                hTmpHandle,
                GetCurrentProcess(),
                &m_ReadHandle,  // Address of new handle.
                0,
                FALSE,  // Make it uninheritable.
                DUPLICATE_SAME_ACCESS))
            return HRESULT_FROM_WIN32(GetLastError());

        // now we kill the original, inheritable server-side handle.  That will keep the
        // pipe open, but keep the client program from getting it.  Child-proofing.
        // Close inheritable copies of the handles you do not want to be inherited.
        if (!CloseHandle(hTmpHandle))
            return HRESULT_FROM_WIN32(GetLastError());

        SetStatus(PipeCreated);
    }
    else
    {
        pipeName = fmt::format(L"\\\\.\\pipe\\DFIR-ORC_{}_ORC_input_{}", GetCurrentProcessId(), szUniqueSuffix);

        // exact same thing, reversed.
        if ((hTmpHandle = CreateNamedPipe(
                 pipeName.c_str(),
                 PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                 pipeMode,
                 PIPE_UNLIMITED_INSTANCES,
                 BUFFER_SIZE,
                 BUFFER_SIZE,
                 0,
                 &sa))
            == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());

        auto hRead = CreateFileApi(
            pipeName.c_str(),
            FILE_READ_DATA | SYNCHRONIZE,
            0,
            &sa,
            OPEN_EXISTING,  // very important flag!
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
            0  // no template file for OPEN_EXISTING
        );

        if (!hRead)
        {
            return ToHRESULT(hRead.error());
        }

        m_ReadHandle = hRead->release();

        if (!::DuplicateHandle(
                GetCurrentProcess(),
                hTmpHandle,
                GetCurrentProcess(),
                &m_WriteHandle,  // Address of new handle.
                0,
                FALSE,  // Make it uninheritable.
                DUPLICATE_SAME_ACCESS))
            return HRESULT_FROM_WIN32(GetLastError());

        if (!CloseHandle(hTmpHandle))
            return HRESULT_FROM_WIN32(GetLastError());

        SetStatus(PipeCreated);
    }
    return S_OK;
}

HRESULT ProcessRedirect::ChildConnected()
{
    HRESULT hr = E_FAIL;

    if (m_Select & StdInput)
    {
        CloseHandle(m_ReadHandle);
        m_ReadHandle = INVALID_HANDLE_VALUE;
    }
    else if (m_Select & StdOutput && !(m_Select & StdError))
    {
        CloseHandle(m_WriteHandle);
        m_WriteHandle = INVALID_HANDLE_VALUE;
    }
    else if (m_Select & StdOutput && m_Select & StdError)
    {
        CloseHandle(m_WriteHandle);
        m_WriteHandle = INVALID_HANDLE_VALUE;
        CloseHandle(m_DuplicateHandle);
        m_DuplicateHandle = INVALID_HANDLE_VALUE;
    }
    else if (m_Select & StdError)
    {
        CloseHandle(m_WriteHandle);
        m_WriteHandle = INVALID_HANDLE_VALUE;
    }

    if (!BindIoCompletionCallback(m_ReadHandle, ProcessRedirect::FileIOCompletionRoutine, 0L))
        return HRESULT_FROM_WIN32(GetLastError());

    // Child is connected, start reading
    if (!(m_Select & StdInput))
    {
        if (FAILED(hr = SubmitRead()))
            return hr;
    }
    return S_OK;
}

HRESULT ProcessRedirect::Close()
{
    ProcessRedirect::RedirectStatus status = Status();

    if (status >= ProcessRedirect::Closed)
        return S_OK;

    // Closes the read handle once no completion can be in flight: nothing below may run concurrently with
    // the callback, which reads m_pBS.
    DrainPendingIo();

    {
        Concurrency::critical_section::scoped_lock lock(m_cs);
        if (m_WriteHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_WriteHandle);
            m_WriteHandle = INVALID_HANDLE_VALUE;
        }
        if (m_DuplicateHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_DuplicateHandle);
            m_DuplicateHandle = INVALID_HANDLE_VALUE;
        }

        if (m_bCloseStream && m_pBS != NULL)
            m_pBS->Close();
        m_pBS = NULL;
    }

    {
        SetStatus(Closed);
    }

    return S_OK;
}

ProcessRedirect::~ProcessRedirect()
{
    if (Status() != Closed)
        Close();

    // Close() may have run on the completion callback itself (broken pipe), which cannot wait for its own
    // completion: repeat the drain here, where that callback is guaranteed to be another thread.
    DrainPendingIo();

    if (m_WriteHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_WriteHandle);
        m_WriteHandle = INVALID_HANDLE_VALUE;
    }
    if (m_DuplicateHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_DuplicateHandle);
        m_DuplicateHandle = INVALID_HANDLE_VALUE;
    }

    if (m_hIoDrained != NULL)
    {
        CloseHandle(m_hIoDrained);
        m_hIoDrained = NULL;
    }
}
