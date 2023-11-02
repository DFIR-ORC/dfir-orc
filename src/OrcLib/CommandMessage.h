//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <string>
#include <memory>
#include <agents.h>

#include "PriorityBuffer.h"
#include "BoundedBuffer.h"

#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {

class CommandParameter
{
public:
    enum ParamKind
    {
        Executable,
        InFile,
        Argument,
        OutFile,
        OutTempFile,
        OutDirectory,
        StdOut,
        StdErr,
        StdOutErr
    };

    ParamKind Kind;

    std::wstring Keyword;
    std::wstring Name;
    std::wstring Pattern;
    std::wstring MatchPattern;  // Only with OutDirectory
    LONG OrderId;
    bool bHash;

    bool bCabWhenComplete;

    CommandParameter(ParamKind kind)
        : Kind(kind)
        , bHash(false)
        , bCabWhenComplete(true)
        , OrderId(0L) {};

    CommandParameter(CommandParameter&& other)
    {
        Kind = other.Kind;
        std::swap(Keyword, other.Keyword);
        std::swap(Name, other.Name);
        std::swap(Pattern, other.Pattern);
        std::swap(MatchPattern, other.MatchPattern);
        OrderId = other.OrderId;
        bHash = other.bHash;
        bCabWhenComplete = other.bCabWhenComplete;
    }
};

class CommandMessage
{

public:
    typedef enum _Request
    {
        Execute = 0,
        Abort,
        Terminate,
        QueryRunningList,
        RefreshRunningList,
        TerminateAll,
        CancelAnyPendingAndStop,
        Done
    } CmdRequest;

    typedef enum _QueueBehavior
    {
        Enqueue = 0,
        FlushQueue
    } QueueBehavior;

    typedef BoundedBuffer<std::shared_ptr<CommandMessage>> BoundedMessageBuffer;
    typedef PriorityBuffer<std::shared_ptr<CommandMessage>> PriorityMessageBuffer;

    typedef std::shared_ptr<CommandMessage> Message;

    typedef Concurrency::ITarget<Message> ITarget;
    typedef Concurrency::ISource<Message> ISource;

    typedef std::vector<CommandParameter> Parameters;

public:
    CommandMessage(CommandMessage&& other)
    {
        m_Request = other.m_Request;
        std::swap(m_Keyword, other.m_Keyword);
        std::swap(m_Parameters, other.m_Parameters);
        std::swap(m_QueueAction, other.m_QueueAction);
        m_dwPid = other.m_dwPid;
    };

    static Message MakeCancelMessage();
    static Message MakeAbortMessage(const std::wstring& keyword, DWORD processId, HANDLE hProcess);
    static Message MakeTerminateMessage(DWORD dwProcessID);
    static Message MakeCancelAnyPendingAndStopMessage();
    static Message MakeTerminateAllMessage();
    static Message MakeRefreshRunningList();
    static Message MakeDoneMessage();
    static Message MakeQueryRunningListMessage();

    static Message MakeExecuteMessage(const std::wstring& keyword);

    HRESULT PushExecutable(const LONG OrderID, const std::wstring& szBinary, const std::wstring& Keyword);

    HRESULT PushArgument(const LONG OrderID, const std::wstring& Keyword);

    HRESULT PushInputFile(const LONG OrderID, const std::wstring& szFileName, const std::wstring& Keyword);

    HRESULT PushInputFile(
        const LONG OrderID,
        const std::wstring& szFileName,
        const std::wstring& Keyword,
        const std::wstring& pattern);

    HRESULT
    PushOutputFile(const LONG OrderID, const std::wstring& szFileName, const std::wstring& Keyword, bool bHash = false);

    HRESULT PushOutputFile(
        const LONG OrderID,
        const std::wstring& szFileName,
        const std::wstring& Keyword,
        const std::wstring& pattern,
        bool bHash = false);

    HRESULT PushOutputDirectory(
        const LONG OrderID,
        const std::wstring& szFileName,
        const std::wstring& Keyword,
        const std::wstring& FileMatchPattern,
        bool bHash = false);

    HRESULT PushOutputDirectory(
        const LONG OrderID,
        const std::wstring& szFileName,
        const std::wstring& Keyword,
        const std::wstring& FileMatchPattern,
        const std::wstring& ArgPattern,
        bool bHash = false);

    HRESULT PushTempOutputFile(
        const LONG OrderID,
        const std::wstring& szFileName,
        const std::wstring& Keyword,
        bool bHash = false);

    HRESULT PushTempOutputFile(
        const LONG OrderID,
        const std::wstring& szFileName,
        const std::wstring& Keyword,
        const std::wstring& pattern,
        bool bHash = false);

    HRESULT PushStdOut(const LONG OrderID, const std::wstring& Keyword, bool bCabWhenComplete, bool bHash = false);

    HRESULT PushStdErr(const LONG OrderID, const std::wstring& Keyword, bool bCabWhenComplete, bool bHash = false);

    HRESULT PushStdOutErr(const LONG OrderID, const std::wstring& Keyword, bool bCabWhenComplete, bool bHash = false);

    HRESULT SetQueueBehavior(const QueueBehavior behavior)
    {
        m_QueueAction = behavior;
        return S_OK;
    };

    QueueBehavior GetQueueBehavior() { return m_QueueAction; };

    void SetOptional() { m_bOptional = true; }
    void SetMandatory() { m_bOptional = false; }
    bool IsOptional() { return m_bOptional; }

    void SetIsSelfOrcExecutable(bool value) { m_isSelfOrcExecutable = value; }
    bool IsSelfOrcExecutable() const { return m_isSelfOrcExecutable; }

    const std::optional<std::wstring>& OrcTool() const { return m_orcTool; }
    void SetOrcTool(const std::wstring& tool) { m_orcTool = tool; }

    void SetTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }
    const std::optional<std::chrono::milliseconds>& GetTimeout() const { return m_timeout; }

    const Parameters& GetParameters() { return m_Parameters; };

    CmdRequest Request() const { return m_Request; };
    const std::wstring& Keyword() const { return m_Keyword; };

    DWORD ProcessID() { return m_dwPid; };
    HANDLE ProcessHandle() const { return m_hProcess; }

    bool operator<(const CommandMessage& message) { return m_Request < message.m_Request; }

    ~CommandMessage(void);

protected:
    CommandMessage(CmdRequest request);

private:
    CmdRequest m_Request;

    std::wstring m_Keyword;

    Parameters m_Parameters;

    QueueBehavior m_QueueAction;

    bool m_bOptional = false;

    bool m_isSelfOrcExecutable = false;
    std::optional<std::wstring> m_orcTool;

    DWORD m_dwPid;
    HANDLE m_hProcess;
    std::optional<std::chrono::milliseconds> m_timeout;
};

}  // namespace Orc

#pragma managed(pop)
