//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "ByteStream.h"

#include "ArchiveMessage.h"

auto constexpr BUFFER_SIZE = 10240;

#pragma managed(push, off)

namespace Orc {

class CByteStream;
class CommandExecute;
class ProcessRedirect;

typedef struct _PROCESS_REDIRECT
{
    OVERLAPPED OL;
    ProcessRedirect* pThis;
    BYTE Buffer[BUFFER_SIZE];
} PROCESS_REDIRECT;

class ORCLIB_API ProcessRedirect
{

    friend class CommandExecute;

public:
    enum ProcessInOut
    {
        StdInput = 0x1 << 0,
        StdOutput = 0x1 << 1,
        StdError = 0x1 << 2
    };

    enum RedirectStatus
    {
        Initialized = 0,
        PipeCreated,
        PendingIO,
        Complete,
        Closed
    };

public:
    static std::shared_ptr<ProcessRedirect>
    MakeRedirect(ProcessInOut selection, std::shared_ptr<ByteStream> pBS, bool bClose = true);

    ProcessInOut Selection() { return m_Select; };

    bool IsComplete() { return m_Status >= Complete; };
    RedirectStatus Status() { return m_Status; };

    HANDLE GetChildHandleFor(ProcessInOut selection);
    HANDLE GetParentHandleFor(ProcessInOut selection);

    HRESULT CreatePipe(const WCHAR* szUniqueSuffix);
    HRESULT ChildConnected();

    std::shared_ptr<ByteStream> GetStream() const { return m_pBS; };

    HRESULT Close();
    ~ProcessRedirect();

private:
    HANDLE m_ReadHandle = INVALID_HANDLE_VALUE;
    HANDLE m_WriteHandle = INVALID_HANDLE_VALUE;
    HANDLE m_DuplicateHandle = INVALID_HANDLE_VALUE;
    ProcessInOut m_Select = ProcessInOut::StdOutput;

    std::shared_ptr<ByteStream> m_pBS;
    bool m_bCloseStream = true;

    PROCESS_REDIRECT m_ASyncIO {0};

    RedirectStatus m_Status = RedirectStatus::Closed;

    Concurrency::critical_section m_cs;

    void SetStatus(RedirectStatus status);

    static VOID CALLBACK FileIOCompletionRoutine(
        __in DWORD dwErrorCode,
        __in DWORD dwNumberOfBytesTransfered,
        __in LPOVERLAPPED lpOverlapped);

    HRESULT ProcessIncomingData(__in DWORD dwErrorCode, __in DWORD dwNumberOfBytesTransfered);

protected:
    ProcessRedirect(ProcessInOut selection);
};

}  // namespace Orc

#pragma managed(pop)
