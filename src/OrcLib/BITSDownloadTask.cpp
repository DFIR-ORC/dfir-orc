//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "BITSDownloadTask.h"
#include "ParameterCheck.h"
#include <Bits.h>
#include <Winhttp.h>

#include <sstream>

#include <boost/scope_exit.hpp>

using namespace std;

using namespace Orc;

#pragma comment(lib, "BITS.lib")

class CDownloadNotifyInterface : public IBackgroundCopyCallback2
{
private:
    LONG m_lRefCount = 0L;
    LONG m_PendingJobModificationCount = 0L;

    std::shared_ptr<DownloadTask> m_task;

public:
    // Constructor, Destructor
    CDownloadNotifyInterface(const std::shared_ptr<DownloadTask>& task)
        : m_task(task) {};

    ~CDownloadNotifyInterface() {};

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IBackgroundCopyCallback methods
    STDMETHOD(JobTransferred)(IBackgroundCopyJob* pJob);
    STDMETHOD(JobError)(IBackgroundCopyJob* pJob, IBackgroundCopyError* pError);
    STDMETHOD(JobModification)(IBackgroundCopyJob* pJob, DWORD dwReserved);
    STDMETHOD(FileTransferred)(IBackgroundCopyJob* pJob, IBackgroundCopyFile* pFile);
};

BITSDownloadTask::BITSDownloadTask(std::wstring strJobName)
    : DownloadTask(std::move(strJobName))
{
}

std::wstring BITSDownloadTask::GetRemoteFullPath(const std::wstring& strRemoteName)
{
    wstringstream stream;
    switch (m_Protocol)
    {
        case BITSProtocol::BITS_HTTP:
            stream << L"http://" << m_strServer << m_strPath << L"/" << strRemoteName;
            break;
        case BITSProtocol::BITS_HTTPS:
            stream << L"https://" << m_strServer << m_strPath << L"/" << strRemoteName;
            break;
        case BITSProtocol::BITS_SMB:
            stream << L"\\\\" << m_strServer << m_strPath << L"\\" << strRemoteName;
            break;
    }
    return stream.str();
}

std::wstring BITSDownloadTask::GetCompletionCommandLine(const GUID& JobId)
{
    HRESULT hr = E_FAIL;

    WCHAR szJobGUID[MAX_PATH];
    wstringstream cmdLine;

    std::wstring strCmdSpec;
    if (FAILED(hr = ExpandFilePath(L"%ComSpec%", strCmdSpec)))
    {
        spdlog::error(L"Failed to determine command");
    }

    if (!StringFromGUID2(JobId, szJobGUID, MAX_PATH))
    {
        spdlog::error(L"Failed to copy GUID to a string");
        return std::wstring();
    }
    else
    {
        cmdLine << L"\"" << strCmdSpec << L"\" /Q /E:OFF /D /C (bitsadmin /COMPLETE " << szJobGUID;
    }

    if (!m_strCmd.empty())
    {
        cmdLine << L" && " << m_strCmd
                << L") ";  // && ensures that the command line is only executed if bitsadmin /complete succeeded
    }

    if (!m_bDelayedDeletion)
    {
        for (const auto& file : m_files)
        {
            if (file.bDelete)
            {
                cmdLine << L" & del \"" << file.strLocalPath << "\"";  // in any case, attempt to delete files
            }
        }
    }
    return cmdLine.str();
}

HRESULT BITSDownloadTask::Initialize(const bool bDelayedDeletion)
{
    HRESULT hr = E_FAIL;
    m_bDelayedDeletion = bDelayedDeletion;

    // Specify the appropriate COM threading model for your application.
    if (FAILED(hr = CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        return hr;
    }

    if (FAILED(
            hr = CoCreateInstance(
                CLSID_BackgroundCopyManager2_5,
                NULL,
                CLSCTX_LOCAL_SERVER,
                __uuidof(IBackgroundCopyManager),
                (void**)&m_pbcm)))
    {
        if (hr == REGDB_E_CLASSNOTREG)
        {
            // if BITS 2.5 is not registered, we fall back to BITS 2.0 which should mostly be OK
            hr = CoCreateInstance(
                CLSID_BackgroundCopyManager2_0,
                NULL,
                CLSCTX_LOCAL_SERVER,
                __uuidof(IBackgroundCopyManager),
                (void**)&m_pbcm);
            if (FAILED(hr))
            {
                spdlog::error(L"Failed to create instance of background copy manager 2.0");
                return hr;
            }
        }
        else
        {
            spdlog::error(L"Failed to create instance of background copy manager 2.5");
            return hr;
        }
    }

    if (m_pbcm == nullptr)
        return E_POINTER;

    GUID JobId;

    // Upload job only support one file transferred at a time
    // we need to create one per file uploaded
    if (FAILED(hr = m_pbcm->CreateJob(m_strJobName.c_str(), BG_JOB_TYPE_DOWNLOAD, &JobId, &m_job)))
    {
        spdlog::error(L"Failed to create job '{}' for background copy (code: {:#x})", m_strJobName, hr);
        return hr;
    }

    for (const auto& file : m_files)
    {
        auto strRemoteFullPath = GetRemoteFullPath(file.strRemoteName);

        if (FAILED(hr = m_job->AddFile(strRemoteFullPath.c_str(), file.strLocalPath.c_str())))
        {
            spdlog::error(
                L"Failed to add download of '{}' to '{}' (code: {:#x})", strRemoteFullPath, file.strLocalPath, hr);
            return hr;
        }
    }

    CComQIPtr<IBackgroundCopyJob2> job2 = m_job;
    ULONG ulNotifyFlags = 0L;
    CComPtr<IBackgroundCopyCallback2> pNotify;

    pNotify = new CDownloadNotifyInterface(shared_from_this());

    if (SUCCEEDED(hr = m_job->SetNotifyInterface(pNotify)))
    {
        ulNotifyFlags |= BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
        if (FAILED(hr = m_job->SetNotifyFlags(ulNotifyFlags)))
        {
            spdlog::error(L"Failed to SetNotifyFlags");
        }
    }
    else
    {
        spdlog::error(L"Failed to SetNotifyInterface");
    }

    if (job2 == nullptr)
    {
        spdlog::error("BITS version does not allow for command notification, uploaded file won't be deleted");
    }
    else
    {
        std::wstring strCmdSpec;
        if (FAILED(hr = ExpandFilePath(L"%ComSpec%", strCmdSpec)))
        {
            spdlog::error(L"Failed to determine command (code: {:#x})", hr);
        }

        auto strCmdLine = GetCompletionCommandLine(JobId);

        if (!strCmdLine.empty() && !strCmdSpec.empty())
        {
            if (FAILED(hr = job2->SetNotifyCmdLine(strCmdSpec.c_str(), strCmdLine.c_str())))
            {
                spdlog::error(L"Failed to SetNotifyCmdLine to delete uploaded files (code: {:#x})", hr);
            }
            else
            {
                ulNotifyFlags |= BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
                if (FAILED(hr = job2->SetNotifyFlags(ulNotifyFlags)))
                {
                    spdlog::error(L"Failed to SetNotifyFlags to delete uploaded files (code: {:#x})", hr);
                }
                // We're done, files will be deleted on upload completion
            }
        }
    }

    pNotify.Release();

    if (FAILED(hr = m_job->Resume()))
    {
        spdlog::error(L"Failed to start upload transfer (code: {:#x})", hr);
    }
    return S_OK;
}

HRESULT BITSDownloadTask::Finalise()
{
    return S_OK;
}

BITSDownloadTask::~BITSDownloadTask() {}

#include "FileCopyDownloadTask.h"

std::shared_ptr<DownloadTask> BITSDownloadTask::GetRetryTask()
{
    if (m_Protocol == BITSProtocol::BITS_SMB)
    {
        // SMB always makes it possible to attempt copy file over SMB
        auto retval = std::make_shared<FileCopyDownloadTask>(m_strJobName.c_str());

        retval->m_files = m_files;
        retval->m_Protocol = DownloadTask::BITSProtocol::BITS_SMB;
        retval->m_strCmd = m_strCmd;
        retval->m_strPath = m_strPath;
        retval->m_strServer = m_strServer;

        return retval;
    }
    return nullptr;
}

//
// Notification interface
//
constexpr auto TWO_GB = 2LU * 1024 * 1024 * 1024;  // 2GB

HRESULT CDownloadNotifyInterface::QueryInterface(REFIID riid, LPVOID* ppvObj)
{
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IBackgroundCopyCallback)
        || riid == __uuidof(IBackgroundCopyCallback2))
    {
        *ppvObj = this;
    }
    else
    {
        *ppvObj = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return NOERROR;
}

ULONG CDownloadNotifyInterface::AddRef()
{
    return InterlockedIncrement(&m_lRefCount);
}

ULONG CDownloadNotifyInterface::Release()
{
    ULONG ulCount = InterlockedDecrement(&m_lRefCount);

    if (0 == ulCount)
    {
        delete this;
    }

    return ulCount;
}

HRESULT CDownloadNotifyInterface::JobTransferred(IBackgroundCopyJob* pJob)
{
    HRESULT hr;

    // Add logic that will not block the callback thread. If you need to perform
    // extensive logic at this time, consider creating a separate thread to perform
    // the work.
    hr = pJob->Complete();
    if (FAILED(hr))
    {
        // Handle error. BITS probably was unable to rename one or more of the
        // temporary files. See the Remarks section of the IBackgroundCopyJob::Complete
        // method for more details.
        return hr;
    }

    // If you do not return S_OK, BITS continues to call this callback.
    return S_OK;
}

HRESULT CDownloadNotifyInterface::JobError(IBackgroundCopyJob* pJob, IBackgroundCopyError* pError)
{
    HRESULT hr;
    BG_FILE_PROGRESS Progress;
    BG_ERROR_CONTEXT Context;
    HRESULT ErrorCode = S_OK;
    WCHAR* pszJobName = NULL;
    WCHAR* pszErrorDescription = NULL;
    BOOL IsError = TRUE;

    // Use pJob and pError to retrieve information of interest. For example,
    // if the job is an upload reply, call the IBackgroundCopyError::GetError method
    // to determine the context in which the job failed. If the context is
    // BG_JOB_CONTEXT_REMOTE_APPLICATION, the server application that received the
    // upload file failed.

    hr = pError->GetError(&Context, &ErrorCode);
    if (FAILED(hr))
        spdlog::error(L"IBackgroundCopyError::GetError failed (code: {:#x})", hr);

    // If the proxy or server does not support the Content-Range header or if
    // antivirus software removes the range requests, BITS returns BG_E_INSUFFICIENT_RANGE_SUPPORT.
    // This implementation tries to switch the job to foreground priority, so
    // the content has a better chance of being successfully downloaded.
    if (BG_E_INSUFFICIENT_RANGE_SUPPORT == ErrorCode)
    {
        CComPtr<IBackgroundCopyFile> pFile;

        hr = pError->GetFile(&pFile);
        if (FAILED(hr))
            spdlog::error(L"IBackgroundCopyError::GetFile failed (code: {:#x})", hr);
        hr = pFile->GetProgress(&Progress);
        if (FAILED(hr))
            spdlog::error(L"IBackgroundCopyError::GetProgress failed (code: {:#x})", hr);

        if (BG_SIZE_UNKNOWN == Progress.BytesTotal)
        {
            // The content is dynamic, do not change priority. Handle as an error.
        }
        else if (Progress.BytesTotal > TWO_GB)
        {
            // BITS does not use range requests if the content is less than 2 GB.
            // However, if the content is greater than 2 GB, BITS
            // uses 2 GB ranges to download the file, so switching to foreground
            // priority will not help.
        }
        else
        {
            spdlog::debug(L"Background upload failed, switching to foreground priority");
            hr = pJob->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
            hr = pJob->Resume();
            IsError = FALSE;
        }
    }

    if (TRUE == IsError)
    {
        hr = pJob->GetDisplayName(&pszJobName);
        if (FAILED(hr))
            spdlog::error(L"IBackgroundCopyJob::GetDisplayName failed (code: {:#x})", hr);

        BOOST_SCOPE_EXIT(&pszJobName) { CoTaskMemFree(pszJobName); }
        BOOST_SCOPE_EXIT_END;

        hr = pError->GetErrorDescription(LANGIDFROMLCID(GetThreadLocale()), &pszErrorDescription);
        if (FAILED(hr))
            spdlog::error(L"IBackgroundCopyJob::GetErrorDescription failed (code: {:#x})", hr);

        BOOST_SCOPE_EXIT(&pszErrorDescription) { CoTaskMemFree(pszErrorDescription); }
        BOOST_SCOPE_EXIT_END;

        if (pszJobName && pszErrorDescription)
        {
            spdlog::error(L"Bits job '{}' with message: {}", pszJobName, pszErrorDescription);
        }
    }

    // If you do not return S_OK, BITS continues to call this callback.
    return S_OK;
}

HRESULT CDownloadNotifyInterface::JobModification(IBackgroundCopyJob* pJob, DWORD dwReserved)
{
    DBG_UNREFERENCED_PARAMETER(dwReserved);
    HRESULT hr = E_FAIL;
    WCHAR* pszJobName = NULL;
    BG_JOB_PROGRESS Progress;
    BG_JOB_STATE State;

    ZeroMemory(&Progress, sizeof(BG_JOB_PROGRESS));

    // If you are already processing a callback, ignore this notification.
    if (InterlockedCompareExchange(&m_PendingJobModificationCount, 1, 0) == 1)
    {
        return S_OK;
    }

    hr = pJob->GetDisplayName(&pszJobName);
    if (SUCCEEDED(hr))
    {
        hr = pJob->GetProgress(&Progress);
        if (SUCCEEDED(hr))
        {
            hr = pJob->GetState(&State);
            if (SUCCEEDED(hr))
            {
                // Do something with the progress and state information.
                // BITS generates a high volume of modification
                // callbacks. Use this callback with discretion. Consider creating a timer and
                // polling for state and progress information.
            }
        }
        CoTaskMemFree(pszJobName);
    }

    m_PendingJobModificationCount = 0;
    return S_OK;
}

HRESULT CDownloadNotifyInterface::FileTransferred(IBackgroundCopyJob* pJob, IBackgroundCopyFile* pFile)
{
    DBG_UNREFERENCED_PARAMETER(pJob);
    HRESULT hr = S_OK;

    CComQIPtr<IBackgroundCopyFile3> pFile3 = pFile;
    if (pFile3)
    {
        // Add code to validate downloaded content and set IsValid.
        BG_FILE_PROGRESS Progress;
        ZeroMemory(&Progress, sizeof(BG_JOB_PROGRESS));

        if (SUCCEEDED(hr = pFile->GetProgress(&Progress)))
        {
            if (Progress.Completed)
                if (FAILED(hr = pFile3->SetValidationState(TRUE)))
                {
                    // Handle error
                }
        }
    }

    return S_OK;
}
