//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once
#include "UploadAgent.h"

#include <bits.h>

#pragma managed(push, off)

namespace Orc {

class BITSJob
{
    friend class BITSAgent;

private:
    const GUID& m_id;
    CComPtr<IBackgroundCopyJob> m_job;

public:
    BITSJob(const GUID& id, const CComPtr<IBackgroundCopyJob>& job)
        : m_id(id)
        , m_job(job) {};
};

class ORCLIB_API BITSAgent : public UploadAgent
{

private:
    CComPtr<IBackgroundCopyManager> m_pbcm;
    bool m_bAddedConnection = false; // When using BITS over SMB, we have to add the connection (net use) to allow test of existence to succeed

    std::vector<BITSJob> m_jobs;

    HRESULT CheckFileUploadOverHttp(const std::wstring& strRemoteName, PDWORD pdwFileSize = nullptr);
    HRESULT CheckFileUploadOverSMB(const std::wstring& strRemoteName, PDWORD pdwFileSize = nullptr);

public:
    static HRESULT CheckCompatibleVersion();

    BITSAgent(
        logger pLog,
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& target)
        : UploadAgent(std::move(pLog), msgSource, msgTarget, target) {};

    virtual HRESULT Initialize();

    virtual HRESULT
    UploadFile(const std::wstring& strLocalName, const std::wstring& strRemoteName, bool bDeleteWhenCopied);

    virtual HRESULT IsComplete(bool bReadyToExit);

    virtual HRESULT Cancel();

    virtual HRESULT UnInitialize();

    virtual HRESULT CheckFileUpload(const std::wstring& strRemoteName, PDWORD pdwFileSize = nullptr);
    virtual std::wstring GetRemoteFullPath(const std::wstring& strRemoteName);
    virtual std::wstring GetRemotePath(const std::wstring& strRemoteName);

    ~BITSAgent();
};

}  // namespace Orc

#pragma managed(pop)
