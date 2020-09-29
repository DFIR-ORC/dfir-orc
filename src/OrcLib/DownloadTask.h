//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#pragma managed(push, off)

namespace Orc {

class FileCopyDownloadTask;
class BITSDownloadTask;
class ConfigItem;

class DownloadTask : public std::enable_shared_from_this<DownloadTask>
{
    friend class FileCopyDownloadTask;
    friend class BITSDownloadTask;

public:
    enum Method
    {
        BITS,
        FILECOPY
    };
    enum BITSProtocol
    {
        BITS_HTTP,
        BITS_HTTPS,
        BITS_SMB
    };

    class DownloadFile
    {
    public:
        DownloadFile(const std::wstring& strRemote, const std::wstring& strLocal, bool bDel) noexcept
            : strRemoteName(strRemote)
            , strLocalPath(strLocal)
            , bDelete(bDel) {};
        DownloadFile(std::wstring&& strRemote, std::wstring&& strLocal, bool bDel) noexcept
            : strRemoteName(strRemote)
            , strLocalPath(strLocal)
            , bDelete(bDel) {};

        std::wstring strRemoteName;
        std::wstring strLocalPath;
        bool bDelete = true;
        bool bTransferred = false;
    };

protected:
    std::wstring m_strJobName;
    std::vector<DownloadFile> m_files;

    std::wstring m_strCmd;

    std::wstring m_strServer;
    std::wstring m_strPath;

    BITSProtocol m_Protocol;

public:
    static std::shared_ptr<DownloadTask> GetTaskFromConfig(const ConfigItem& item);

    virtual std::shared_ptr<DownloadTask> GetRetryTask() PURE;

    DownloadTask(std::wstring strJobName)
        : m_strJobName(std::move(strJobName))
    {
    }

    HRESULT AddFile(const std::wstring& strRemotePath, const std::wstring& strLocalPath, const bool bDelete = true);
    HRESULT SetCommand(const std::wstring& strCommandPath)
    {
        m_strCmd = strCommandPath;
        return S_OK;
    }

    const std::vector<DownloadFile>& Files() const { return m_files; }

    virtual HRESULT
    Initialize(const bool bDelayedDeletion) PURE;  // bDelayedDeletion means that the delete task does _not_ handle the
                                                   // deletetions and this task is delegated to other portions of code
    virtual HRESULT Finalise() PURE;

    ~DownloadTask() {};
};

}  // namespace Orc
#pragma managed(pop)
