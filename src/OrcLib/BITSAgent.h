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

class BITSAgent : public UploadAgent
{

private:
    CComPtr<IBackgroundCopyManager> m_pbcm;
    bool m_bAddedConnection = false;  // When using BITS over SMB, we have to add the connection (net use) to allow test
                                      // of existence to succeed

    std::vector<BITSJob> m_jobs;

    HRESULT CheckFileUploadOverHttp(const std::wstring& strRemoteName, PDWORD pdwFileSize = nullptr);
    HRESULT CheckFileUploadOverSMB(const std::wstring& strRemoteName, PDWORD pdwFileSize = nullptr);

public:
    static HRESULT CheckCompatibleVersion();

    BITSAgent(UploadMessage::ISource& msgSource, UploadMessage::ITarget& msgTarget, UploadNotification::ITarget& target)
        : UploadAgent(msgSource, msgTarget, target) {};

    HRESULT Initialize() override;

    HRESULT
    UploadFile(
        const std::wstring& strLocalName,
        const std::wstring& strRemoteName,
        bool bDeleteWhenCopied,
        const std::shared_ptr<const UploadMessage>& request) override;

    HRESULT IsComplete(bool bReadyToExit, bool& hasFailure) override;

    HRESULT Cancel() override;

    HRESULT UnInitialize() override;

    HRESULT CheckFileUpload(const std::wstring& strRemoteName, PDWORD pdwFileSize = nullptr) override;
    std::wstring GetRemoteFullPath(const std::wstring& strRemoteName) override;
    std::wstring GetRemotePath(const std::wstring& strRemoteName) override;

    ~BITSAgent();
};

}  // namespace Orc

#pragma managed(pop)
