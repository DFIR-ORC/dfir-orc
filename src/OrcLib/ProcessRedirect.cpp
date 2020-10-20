//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <memory>
#include <agents.h>

#include "ProcessRedirect.h"

#include "CommandExecute.h"

#include "ByteStream.h"

#include <spdlog/spdlog.h>

using namespace std;
using namespace Concurrency;

using namespace Orc;

static const NTSTATUS STATUS_PIPE_BROKEN = 0xC000014BL;

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
    HRESULT hr = E_FAIL;

    if (ERROR_SUCCESS == dwErrorCode)
    {
        if (m_pBS != NULL)
        {
            ULONGLONG ullBytesWritten = 0L;
            if (FAILED(hr = m_pBS->Write(m_ASyncIO.Buffer, dwNumberOfBytesTransfered, &ullBytesWritten)))
            {
                spdlog::warn(L"Failed to write incoming data (code: {:#x})", hr);
            }
        }
        if (m_ReadHandle != INVALID_HANDLE_VALUE)
        {
            DWORD dwBytesRead = 0L;
            if (!ReadFile(m_ReadHandle, m_ASyncIO.Buffer, BUFFER_SIZE, &dwBytesRead, &m_ASyncIO.OL))
            {
                DWORD dwLastError = GetLastError();
                switch (dwLastError)
                {
                    case ERROR_IO_PENDING:
                    case ERROR_MORE_DATA:
                        SetStatus(PendingIO);
                        break;
                    case ERROR_BROKEN_PIPE:
                        SetStatus(Complete);
                        Close();
                        break;
                    default:
                        spdlog::error(L"ReadFile from child failed (code: {:#x})", HRESULT_FROM_WIN32(dwLastError));
                        break;
                }
            }
        }
    }
    else
    {
        if (m_ReadHandle != INVALID_HANDLE_VALUE)
        {
            DWORD dwBytesRead = 0L;
            if (!ReadFile(m_ReadHandle, m_ASyncIO.Buffer, BUFFER_SIZE, &dwBytesRead, &m_ASyncIO.OL))
            {
                DWORD dwLastError = GetLastError();
                switch (dwLastError)
                {
                    case ERROR_IO_PENDING:
                    case ERROR_MORE_DATA:
                        SetStatus(PendingIO);
                        break;
                    case ERROR_BROKEN_PIPE:
                        SetStatus(Complete);
                        Close();
                        break;
                    default:
                        spdlog::error("ReadFile from child failed (code: {:#x})", HRESULT_FROM_WIN32(dwLastError));
                        break;
                }
            }
        }
    }
    return S_OK;
}

VOID ProcessRedirect::FileIOCompletionRoutine(
    __in DWORD dwErrorCode,
    __in DWORD dwNumberOfBytesTransfered,
    __in LPOVERLAPPED lpOverlapped)
{
    PROCESS_REDIRECT* pASyncIO = (PROCESS_REDIRECT*)lpOverlapped;

    pASyncIO->pThis->ProcessIncomingData(dwErrorCode, dwNumberOfBytesTransfered);
    return;
}

void ProcessRedirect::SetStatus(RedirectStatus status)
{
    ArchiveMessage::Message request;
    {
        if (m_Status >= status)
            return;
        else
        {
            m_Status = status;
        }
    }
}

HRESULT ProcessRedirect::CreatePipe(const WCHAR* szUniqueSuffix)
{
    WCHAR szPipeName[MAX_PATH];
    ZeroMemory(szPipeName, MAX_PATH * sizeof(WCHAR));

    if (m_Select & StdInput && (m_Select & StdOutput || m_Select & StdError))
        return E_INVALIDARG;

    // you need this for the client to inherit the handles	SECURITY_ATTRIBUTES sa;
    SECURITY_ATTRIBUTES sa;
    // Set up the security attributes struct.
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;  // this is the critical bit
    sa.lpSecurityDescriptor = NULL;
    //
    // this creates a inheritable, one-way handle for the server to read
    //
    HANDLE hTmpHandle = INVALID_HANDLE_VALUE;

    if ((m_Select & StdOutput) || (m_Select & StdError))
    {
        if ((m_Select & StdOutput) && (m_Select & StdError))
            wcscpy_s(szPipeName, L"\\\\.\\pipe\\DFIR-ORC_out_err_");
        else if (m_Select & StdOutput)
            wcscpy_s(szPipeName, L"\\\\.\\pipe\\DFIR-ORC_output_");
        else if (m_Select & StdError)
            wcscpy_s(szPipeName, L"\\\\.\\pipe\\DFIR-ORC_error_");

        wcscat_s(szPipeName, szUniqueSuffix);

        if ((hTmpHandle = CreateNamedPipe(
                 szPipeName,
                 PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
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
        m_WriteHandle = CreateFile(
            szPipeName,
            FILE_WRITE_DATA | SYNCHRONIZE,
            0,
            &sa,
            OPEN_EXISTING,  // very important flag!
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            0  // no template file for OPEN_EXISTING
        );

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
        wcscpy_s(szPipeName, L"\\\\.\\pipe\\ORC_input_");
        wcscat_s(szPipeName, szUniqueSuffix);

        // exact same thing, reversed.
        if ((hTmpHandle = CreateNamedPipe(
                 szPipeName,
                 PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
                 PIPE_UNLIMITED_INSTANCES,
                 BUFFER_SIZE,
                 BUFFER_SIZE,
                 0,
                 &sa))
            == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());

        m_ReadHandle = CreateFile(
            szPipeName,
            FILE_READ_DATA | SYNCHRONIZE,
            0,
            &sa,
            OPEN_EXISTING,  // very important flag!
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            0  // no template file for OPEN_EXISTING
        );

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
        DWORD dwBytesRead = 0L;
        if (!ReadFile(m_ReadHandle, m_ASyncIO.Buffer, BUFFER_SIZE, &dwBytesRead, &m_ASyncIO.OL))
        {
            if (GetLastError() != ERROR_IO_PENDING)
                return HRESULT_FROM_WIN32(GetLastError());
            else
                SetStatus(PendingIO);
        }
        else
        {
            if (FAILED(hr = ProcessIncomingData(ERROR_SUCCESS, dwBytesRead)))
                return hr;
        }
    }
    return S_OK;
}

HRESULT ProcessRedirect::Close()
{
    ProcessRedirect::RedirectStatus status = Status();

    if (status >= ProcessRedirect::Closed)
        return S_OK;

    {
        Concurrency::critical_section::scoped_lock lock(m_cs);
        if (m_ReadHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_ReadHandle);
            m_ReadHandle = INVALID_HANDLE_VALUE;
        }
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

    if (m_ReadHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_ReadHandle);
        m_ReadHandle = INVALID_HANDLE_VALUE;
    }
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
}
