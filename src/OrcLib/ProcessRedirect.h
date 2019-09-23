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

class LogFileWriter;
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
    friend class std::shared_ptr<ProcessRedirect>;
    friend class std::_Ref_count_obj<ProcessRedirect>;

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
    MakeRedirect(logger pLog, ProcessInOut selection, std::shared_ptr<ByteStream> pBS, bool bClose = true);

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
    HANDLE m_ReadHandle;
    HANDLE m_WriteHandle;
    HANDLE m_DuplicateHandle;
    ProcessInOut m_Select;

    std::shared_ptr<ByteStream> m_pBS;
    bool m_bCloseStream;

    PROCESS_REDIRECT m_ASyncIO;

    RedirectStatus m_Status;

    logger _L_;

    Concurrency::critical_section m_cs;

    void SetStatus(RedirectStatus status);

    static VOID CALLBACK FileIOCompletionRoutine(
        __in DWORD dwErrorCode,
        __in DWORD dwNumberOfBytesTransfered,
        __in LPOVERLAPPED lpOverlapped);

    HRESULT ProcessIncomingData(__in DWORD dwErrorCode, __in DWORD dwNumberOfBytesTransfered);

    ProcessRedirect(logger pLog, ProcessInOut selection);
};

}  // namespace Orc

#pragma managed(pop)
