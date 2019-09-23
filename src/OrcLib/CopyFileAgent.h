//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "UploadAgent.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API CopyFileAgent : public UploadAgent
{
private:
    bool bAddedConnection = false;

public:
    CopyFileAgent(
        logger pLog,
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& target)
        : UploadAgent(std::move(pLog), msgSource, msgTarget, target) {};
    ~CopyFileAgent();

    virtual HRESULT Initialize();

    virtual HRESULT
    UploadFile(const std::wstring& strLocalName, const std::wstring& strRemoteName, bool bDeleteWhenCopied);

    virtual HRESULT IsComplete(bool bReadyToExit);

    virtual HRESULT Cancel();

    virtual HRESULT UnInitialize();

    virtual HRESULT CheckFileUpload(const std::wstring& szRemoteName, PDWORD pdwFileSize = nullptr);
    virtual std::wstring GetRemoteFullPath(const std::wstring& strRemoteName);
    virtual std::wstring GetRemotePath(const std::wstring& strRemoteName);
};

}  // namespace Orc

#pragma managed(pop)
