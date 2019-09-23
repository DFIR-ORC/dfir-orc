//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ArchiveMessage.h"
#include "ArchiveNotification.h"
#include "ArchiveCreate.h"

#include "Robustness.h"

#include <memory>
#include <string>
#include <agents.h>
#include <concrt.h>

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API ArchiveAgent : public Concurrency::agent
{
    class OnCompleteTerminationHandler;

public:
    class ORCLIB_API OnComplete
    {
        friend class ArchiveAgent;

    public:
        enum Object
        {
            File,
            Directory
        };

        enum Action
        {
            Void,
            Delete
        };

        OnComplete(Action action, Object object, const std::wstring& name, const std::wstring& fullpath);

        OnComplete(OnComplete&& other)
        {
            m_action = other.m_action;
            std::swap(m_Name, other.m_Name);
            std::swap(m_FullPath, other.m_FullPath);
            m_pTerminationHandler = other.m_pTerminationHandler;
            other.m_pTerminationHandler = nullptr;
            m_hr = other.m_hr;
        }

        OnComplete& operator=(const OnComplete&) = default;

        ~OnComplete();

        Action GetAction() const { return m_action; };
        Object GetObjectType() const { return m_object; };

        const std::wstring& Name() const { return m_Name; };
        const std::wstring& Fullpath() const { return m_FullPath; };

        void CancelTerminationHandler();

    private:
        OnComplete(const OnComplete& other) { DBG_UNREFERENCED_PARAMETER(other); };

        Action m_action;
        Object m_object;
        HRESULT m_hr;

        std::wstring m_Name;
        std::wstring m_FullPath;

        std::shared_ptr<OnCompleteTerminationHandler> m_pTerminationHandler;
    };

private:
    class OnCompleteTerminationHandler : public TerminationHandler
    {
    public:
        OnCompleteTerminationHandler(
            const std::wstring& strDescr,
            const ArchiveAgent::OnComplete::Object obj,
            const std::wstring& fullpath)
            : TerminationHandler(strDescr, ROBUSTNESS_TEMPFILE)
            , m_object(obj)
            , m_FullPath(fullpath) {};

        OnCompleteTerminationHandler(
            const std::wstring& strDescr,
            const ArchiveAgent::OnComplete::Object obj,
            const std::shared_ptr<ByteStream>& stream)
            : TerminationHandler(strDescr, ROBUSTNESS_TEMPFILE)
            , m_object(obj)
            , m_Stream(stream) {};

        HRESULT operator()();

    private:
        std::wstring m_FullPath;
        std::shared_ptr<ByteStream> m_Stream;
        ArchiveAgent::OnComplete::Object m_object;
    };

    logger _L_;
    std::wstring m_cabName;
    std::shared_ptr<ArchiveCreate> m_compressor;

    std::vector<OnComplete> m_PendingCompletions;

protected:
    ArchiveNotification::ITarget& m_target;
    ArchiveMessage::ISource& m_source;
    ArchiveMessage::ITarget& m_messagetarget;

    bool SendResult(const ArchiveNotification::Notification& request)
    {
        return Concurrency::send<ArchiveNotification::Notification>(m_target, request);
    }

    ArchiveMessage::Message GetRequest() { return Concurrency::receive<ArchiveMessage::Message>(m_source); }

    HRESULT CompleteOnFlush(bool bFinal);

    virtual void run();

public:
    ArchiveAgent(
        logger pLog,
        ArchiveMessage::ISource& source,
        ArchiveMessage::ITarget& messagetarget,
        ArchiveNotification::ITarget& target)
        : _L_(std::move(pLog))
        , m_source(source)
        , m_messagetarget(messagetarget)
        , m_target(target)
    {
    }

    ~ArchiveAgent(void) {};
};

}  // namespace Orc

#pragma managed(pop)
