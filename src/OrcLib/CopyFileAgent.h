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

class CopyFileAgent : public UploadAgent
{
private:
    bool bAddedConnection = false;

public:
    CopyFileAgent(
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& target)
        : UploadAgent(msgSource, msgTarget, target) {};
    ~CopyFileAgent();

    HRESULT Initialize() override;

    HRESULT UploadFile(
        const std::wstring& strLocalName,
        const std::wstring& strRemoteName,
        bool bDeleteWhenCopied,
        const std::shared_ptr<const UploadMessage>& request) override;

    HRESULT IsComplete(bool bReadyToExit, bool& hasFailure) override;

    HRESULT Cancel() override;

    HRESULT UnInitialize() override;

    HRESULT CheckFileUpload(const std::wstring& szRemoteName, std::optional<DWORD>&) override;
    std::wstring GetRemoteFullPath(const std::wstring& strRemoteName) override;
    std::wstring GetRemotePath(const std::wstring& strRemoteName) override;
};

}  // namespace Orc

#pragma managed(pop)
