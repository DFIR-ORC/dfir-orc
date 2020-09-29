//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "DownloadTask.h"

#pragma managed(push, off)

namespace Orc {

class FileCopyDownloadTask : public DownloadTask
{

public:
    FileCopyDownloadTask(LPCWSTR szJobName)
        : DownloadTask(szJobName) {};

    virtual HRESULT Initialize(const bool bDelayedDeletion);
    virtual HRESULT Finalise();

    virtual std::shared_ptr<DownloadTask> GetRetryTask();

    ~FileCopyDownloadTask() {};

private:
    bool m_bDelayedDeletion = false;
    std::wstring GetRemoteFullPath(const std::wstring& strRemoteName);
    std::wstring GetCompletionCommandLine();
};
}  // namespace Orc
#pragma managed(pop)
