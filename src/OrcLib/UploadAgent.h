//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "UploadMessage.h"
#include "UploadNotification.h"

#include "OutputSpec.h"

#include "Robustness.h"

#include <memory>
#include <set>
#include <string>
#include <agents.h>
#include <concrt.h>

#pragma managed(push, off)

namespace Orc {

class UploadAgent : public Concurrency::agent
{
public:
    static std::shared_ptr<UploadAgent> CreateUploadAgent(
        const OutputSpec::Upload& uploadSpec,
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& target);

    virtual void run();

    // Whether this agent ever queued a FAILED notification. The caller reads it after agent::wait, which
    // orders the read against everything the agent did. Reading the notification handler's own flag
    // instead would race: Concurrency::send returns once the 'call' block ACCEPTS the message, and the
    // block then runs on the scheduler, so a failure sent just before done() can still be in flight when
    // agent::wait returns - and a fallback list would be skipped on the strength of a failure nobody
    // recorded yet. Not atomic on purpose: agent::wait is the synchronisation.
    bool HasNotifiedFailure() const { return m_notifiedFailure; }

    // Local paths this agent confirmed as uploaded. Read after agent::wait, for the same reason and with
    // the same ordering guarantee as HasNotifiedFailure: a retry rebuilds the agent and must not re-send
    // what this one already delivered to the very same destination.
    const std::set<std::wstring>& UploadedFiles() const { return m_uploadedFiles; }

    // The size is 64-bit because repeat="Once" compares it with the local archive: a 32-bit value capped at
    // MAXDWORD would make every archive of 4 GiB or more look incomplete and re-upload it on every run.
    virtual HRESULT CheckFileUpload(const std::wstring& szRemoteName, std::optional<uint64_t>& fileSize) PURE;
    virtual std::wstring GetRemoteFullPath(const std::wstring& strRemoteName) PURE;
    virtual std::wstring GetRemotePath(const std::wstring& strRemoteName) PURE;

    ~UploadAgent()
    {
        // Defensive teardown stop: ensure the repeating status timer cannot fire a tick into a
        // partially-destroyed agent or a freed message buffer. stop() blocks until any in-flight tick
        // completes and is idempotent with the stop() on the run() completion path.
        if (m_RefreshTimer)
        {
            m_RefreshTimer->stop();
        }
    }

protected:
    UploadNotification::ITarget& m_notificationTarget;
    UploadMessage::ITarget& m_requestTarget;
    UploadMessage::ISource& m_requestsource;

    OutputSpec::Upload m_config;

    bool m_notifiedFailure = false;
    std::set<std::wstring> m_uploadedFiles;

    std::unique_ptr<Concurrency::timer<UploadMessage::Message>> m_RefreshTimer;

    bool SendResult(const UploadNotification::Notification& request)
    {
        // Record the outcome here, on the agent, rather than leaving it to whoever consumes the
        // notification (see HasNotifiedFailure).
        if (request && FAILED(request->GetHResult()))
        {
            m_notifiedFailure = true;
        }
        else if (request && request->GetType() == UploadNotification::FileAddition)
        {
            // Source() is the local path here (see run(), which passes LocalName as the source).
            m_uploadedFiles.insert(request->Source());
        }

        return Concurrency::send<UploadNotification::Notification>(m_notificationTarget, request);
    }

    UploadMessage::Message GetRequest() { return Concurrency::receive<UploadMessage::Message>(m_requestsource); }

    HRESULT SetConfiguration(const OutputSpec::Upload& config)
    {
        m_config = config;
        return S_OK;
    }

protected:
    UploadAgent(
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& notifyTarget)
        : m_requestsource(msgSource)
        , m_requestTarget(msgTarget)
        , m_notificationTarget(notifyTarget)
    {
    }

    virtual HRESULT Initialize() PURE;

    virtual HRESULT UploadFile(
        const std::wstring& strLocalName,
        const std::wstring& strRemoteName,
        bool bDeleteWhenCopied,
        const std::shared_ptr<const UploadMessage>& request) PURE;

    virtual HRESULT IsComplete(bool bReadyToExit, bool& hasFailure) PURE;

    virtual HRESULT Cancel() PURE;

    virtual HRESULT UnInitialize() PURE;
};

}  // namespace Orc

#pragma managed(pop)
