//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "DownloadTask.h"

#include <bits.h>

#pragma managed(push, off)

namespace Orc {

class BITSDownloadTask : public DownloadTask
{

public:
    BITSDownloadTask(std::wstring strJobName);

    virtual HRESULT Initialize(const bool bDelayedDeletion);
    virtual HRESULT Finalise();

    virtual std::shared_ptr<DownloadTask> GetRetryTask();

    ~BITSDownloadTask();

private:
    CComPtr<IBackgroundCopyManager> m_pbcm;
    CComPtr<IBackgroundCopyJob> m_job;
    bool m_bDelayedDeletion = false;

    std::wstring GetRemoteFullPath(const std::wstring& strRemoteName);
    std::wstring GetCompletionCommandLine(const GUID& JobId);
};

}  // namespace Orc
#pragma managed(pop)
