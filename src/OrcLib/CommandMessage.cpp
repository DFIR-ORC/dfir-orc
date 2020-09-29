//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "CommandMessage.h"

using namespace Orc;

namespace {

// Enable the use of std::make_shared with CommandMessage protected constructor
struct CommandMessageT : public Orc::CommandMessage
{
    template <typename... Args>
    inline CommandMessageT(Args&&... args)
        : CommandMessage(std::forward<Args>(args)...)
    {
    }
};

}  // namespace

CommandMessage::CommandMessage(CommandMessage::CmdRequest request)
    : m_Request(request)
    , m_dwPid(0L)
    , m_QueueAction(Enqueue)
{
}

CommandMessage::Message CommandMessage::MakeCancelAnyPendingAndStopMessage()
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::CancelAnyPendingAndStop);
    return retval;
}

CommandMessage::Message CommandMessage::MakeTerminateMessage(DWORD dwProcessID)
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::Terminate);
    retval->m_dwPid = dwProcessID;
    return retval;
}

CommandMessage::Message CommandMessage::MakeTerminateAllMessage()
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::TerminateAll);
    return retval;
}

CommandMessage::Message CommandMessage::MakeRefreshRunningList()
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::RefreshRunningList);
    return retval;
}

CommandMessage::Message CommandMessage::MakeDoneMessage()
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::Done);
    return retval;
}

CommandMessage::Message CommandMessage::MakeQueryRunningListMessage()
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::QueryRunningList);
    return retval;
}

CommandMessage::Message CommandMessage::MakeExecuteMessage(const std::wstring& keyword)
{
    auto retval = std::make_shared<::CommandMessageT>(CommandMessage::Execute);
    retval->m_Keyword = keyword;
    return retval;
}

HRESULT CommandMessage::PushExecutable(const LONG OrderID, const std::wstring& szBinary, const std::wstring& Keyword)
{
    CommandParameter input(CommandParameter::Executable);

    input.OrderId = OrderID;
    input.Keyword = Keyword;
    input.Name = szBinary;

    m_Parameters.push_back(std::move(input));
    return S_OK;
}

HRESULT CommandMessage::PushArgument(const LONG OrderID, const std::wstring& Keyword)
{
    CommandParameter input(CommandParameter::Argument);
    input.OrderId = OrderID;
    input.Keyword = Keyword;
    m_Parameters.push_back(std::move(input));
    return S_OK;
}

HRESULT CommandMessage::PushInputFile(
    const LONG OrderID,
    const std::wstring& szName,
    const std::wstring& Keyword,
    const std::wstring& pattern)
{
    CommandParameter input(CommandParameter::InFile);

    input.OrderId = OrderID;
    input.Keyword = Keyword;
    input.Name = szName;
    input.Pattern = pattern;
    m_Parameters.push_back(std::move(input));

    return S_OK;
}

HRESULT CommandMessage::PushInputFile(const LONG OrderID, const std::wstring& szName, const std::wstring& Keyword)
{
    CommandParameter input(CommandParameter::InFile);

    input.OrderId = OrderID;
    input.Keyword = Keyword;
    input.Name = szName;
    m_Parameters.push_back(std::move(input));
    return S_OK;
}

HRESULT CommandMessage::PushOutputFile(
    const LONG OrderID,
    const std::wstring& szName,
    const std::wstring& Keyword,
    const std::wstring& pattern,
    bool bHash)
{
    CommandParameter output(CommandParameter::OutFile);

    output.OrderId = OrderID;
    output.Name = szName;
    output.Keyword = Keyword;
    output.Pattern = pattern;
    output.bHash = bHash;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT
CommandMessage::PushOutputFile(const LONG OrderID, const std::wstring& szName, const std::wstring& Keyword, bool bHash)
{
    CommandParameter output(CommandParameter::OutFile);

    output.OrderId = OrderID;
    output.Name = szName;
    output.Keyword = Keyword;
    output.bHash = bHash;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT CommandMessage::PushOutputDirectory(
    const LONG OrderID,
    const std::wstring& szName,
    const std::wstring& Keyword,
    const std::wstring& filematchPattern,
    const std::wstring& pattern,
    bool bHash)
{
    CommandParameter output(CommandParameter::OutDirectory);

    output.OrderId = OrderID;
    output.Name = szName;
    output.Keyword = Keyword;
    output.Pattern = pattern;
    output.MatchPattern = filematchPattern;
    output.bHash = bHash;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT CommandMessage::PushOutputDirectory(
    const LONG OrderID,
    const std::wstring& szName,
    const std::wstring& Keyword,
    const std::wstring& filematchPattern,
    bool bHash)
{
    CommandParameter output(CommandParameter::OutDirectory);

    output.OrderId = OrderID;
    output.Name = szName;
    output.Keyword = Keyword;
    output.MatchPattern = filematchPattern;
    output.bHash = bHash;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT CommandMessage::PushTempOutputFile(
    const LONG OrderID,
    const std::wstring& szName,
    const std::wstring& Keyword,
    const std::wstring& pattern,
    bool bHash)
{
    CommandParameter output(CommandParameter::OutTempFile);

    output.OrderId = OrderID;
    output.Name = szName;
    output.Keyword = Keyword;
    output.Pattern = pattern;
    output.bHash = bHash;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT CommandMessage::PushTempOutputFile(
    const LONG OrderID,
    const std::wstring& szName,
    const std::wstring& Keyword,
    bool bHash)
{
    CommandParameter output(CommandParameter::OutTempFile);

    output.OrderId = OrderID;
    output.Name = szName;
    output.Keyword = Keyword;
    output.bHash = bHash;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT CommandMessage::PushStdOut(const LONG OrderID, const std::wstring& Keyword, bool bCabWhenComplete, bool bHash)
{
    CommandParameter output(CommandParameter::StdOut);

    output.OrderId = OrderID;
    output.Keyword = Keyword;
    output.Name = Keyword;
    output.bHash = bHash;
    output.bCabWhenComplete = bCabWhenComplete;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT CommandMessage::PushStdErr(const LONG OrderID, const std::wstring& Keyword, bool bCabWhenComplete, bool bHash)
{
    CommandParameter output(CommandParameter::StdErr);

    output.OrderId = OrderID;
    output.Keyword = Keyword;
    output.Name = Keyword;
    output.bHash = bHash;
    output.bCabWhenComplete = bCabWhenComplete;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

HRESULT
CommandMessage::PushStdOutErr(const LONG OrderID, const std::wstring& Keyword, bool bCabWhenComplete, bool bHash)
{
    CommandParameter output(CommandParameter::StdOut);

    output.OrderId = OrderID;
    output.Keyword = Keyword;
    output.Name = Keyword;
    output.bHash = bHash;
    output.bCabWhenComplete = bCabWhenComplete;
    m_Parameters.push_back(std::move(output));
    return S_OK;
}

CommandMessage::~CommandMessage() {}
