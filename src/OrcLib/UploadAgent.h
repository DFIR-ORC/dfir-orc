//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
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
#include <string>
#include <agents.h>
#include <concrt.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API UploadAgent : public Concurrency::agent
{
public:
    static std::shared_ptr<UploadAgent> CreateUploadAgent(
        const OutputSpec::Upload& uploadSpec,
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& target);

    virtual void run();

    virtual HRESULT CheckFileUpload(const std::wstring& szRemoteName, PDWORD pdwFileSize = nullptr) PURE;
    virtual std::wstring GetRemoteFullPath(const std::wstring& strRemoteName) PURE;
    virtual std::wstring GetRemotePath(const std::wstring& strRemoteName) PURE;

    ~UploadAgent() {};

protected:
    UploadNotification::ITarget& m_notificationTarget;
    UploadMessage::ITarget& m_requestTarget;
    UploadMessage::ISource& m_requestsource;

    OutputSpec::Upload m_config;

    std::unique_ptr<Concurrency::timer<UploadMessage::Message>> m_RefreshTimer;

    bool SendResult(const UploadNotification::Notification& request)
    {
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

    virtual HRESULT IsComplete(bool bReadyToExit = false) PURE;

    virtual HRESULT Cancel() PURE;

    virtual HRESULT UnInitialize() PURE;
};

}  // namespace Orc

#pragma managed(pop)
