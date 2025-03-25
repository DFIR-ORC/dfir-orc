//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <sstream>
#include <regex>

#include <Bits.h>
#include <Winhttp.h>
#include <boost/scope_exit.hpp>
#include <boost/algorithm/string.hpp>

#include "BITSAgent.h"
#include "ParameterCheck.h"
#include "Text/Fmt/GUID.h"

using namespace Orc;

#pragma comment(lib, "BITS.lib")

class CNotifyInterface : public IBackgroundCopyCallback2
{
public:
    // Constructor, Destructor
    CNotifyInterface(
        std::wstring strJobName,
        UploadMessage::Ptr request,
        UploadNotification::ITarget& pNotificationTarget,
        bool bDeleteWhenUploaded,
        const std::wstring& source,
        const std::wstring& destination)
        : m_request(request)
        , m_strJobName(strJobName)
        , m_bDeleteWhenUploaded(bDeleteWhenUploaded)
        , m_source(source)
        , m_destination(destination)
        , m_pNotificationTarget(pNotificationTarget) {};

    ~CNotifyInterface() {};

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IBackgroundCopyCallback methods
    STDMETHOD(JobTransferred)(IBackgroundCopyJob* pJob);
    STDMETHOD(JobError)(IBackgroundCopyJob* pJob, IBackgroundCopyError* pError);
    STDMETHOD(JobModification)(IBackgroundCopyJob* pJob, DWORD dwReserved);
    STDMETHOD(FileTransferred)(IBackgroundCopyJob* pJob, IBackgroundCopyFile* pFile);

private:
    LONG m_lRefCount = 0L;
    LONG m_PendingJobModificationCount = 0L;

    UploadMessage::Ptr m_request;
    std::wstring m_strJobName;
    std::wstring m_source;
    std::wstring m_destination;
    bool m_bDeleteWhenUploaded = false;

    UploadNotification::ITarget& m_pNotificationTarget;
};

HRESULT BITSAgent::Initialize()
{
    HRESULT hr = E_FAIL;

    if (m_config.Method != OutputSpec::UploadMethod::BITS)
    {
        Log::Error("BITSAgent failed to initialize because of invalid method");
        return E_UNEXPECTED;
    }

    // Specify the appropriate COM threading model for your application.
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(
            CLSID_BackgroundCopyManager2_5,
            NULL,
            CLSCTX_LOCAL_SERVER,
            __uuidof(IBackgroundCopyManager),
            (void**)&m_pbcm);
        if (FAILED(hr))
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
                    Log::Error(L"BITSAgent failed to create instance of background copy manager 2.0");
                    return hr;
                }
            }
            else
            {
                Log::Error(L"BITSAgent failed to create instance of background copy manager 2.5 [{}]", SystemError(hr));
                return hr;
            }
        }
    }
    else
    {
        Log::Error(L"BITSAgent failed CoInitializeEx [{}]", SystemError(hr));
        return hr;
    }

    // TODO: Mutualize this code between the copyfileagent & bitsagent
    if (m_config.bitsMode == OutputSpec::BITSMode::SMB && !m_config.UserName.empty())
    {
        NETRESOURCE nr;

        nr.dwScope = 0L;
        nr.dwType = RESOURCETYPE_DISK;
        nr.dwDisplayType = 0L;
        nr.dwUsage = 0L;

        nr.lpLocalName = NULL;

        nr.lpComment = NULL;
        nr.lpProvider = NULL;

        std::wstringstream stream;
        WCHAR szUNC[ORC_MAX_PATH];
        ZeroMemory(szUNC, ORC_MAX_PATH * sizeof(WCHAR));

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath;
        stream.str()._Copy_s(szUNC, ORC_MAX_PATH, ORC_MAX_PATH);
        nr.lpRemoteName = szUNC;

        WCHAR szUser[ORC_MAX_PATH];
        WCHAR szPass[ORC_MAX_PATH];
        ZeroMemory(szUser, ORC_MAX_PATH * sizeof(WCHAR));
        ZeroMemory(szPass, ORC_MAX_PATH * sizeof(WCHAR));
        m_config.UserName._Copy_s(szUser, ORC_MAX_PATH, ORC_MAX_PATH);
        szUser[m_config.UserName.size()] = L'\0';
        m_config.Password._Copy_s(szPass, ORC_MAX_PATH, ORC_MAX_PATH);
        szPass[m_config.Password.size()] = L'\0';

        DWORD dwRet = 0;

        if ((dwRet = WNetAddConnection2(&nr, szPass, szUser, CONNECT_TEMPORARY)) != NO_ERROR)
        {
            Log::Error(L"BITSAgent failed to add a connection to {} [{}]", szUNC, Win32Error(dwRet));
        }
        else
        {
            // TODO: remove this diag
            Log::Info(L"Added a connection to {}", szUNC);
            m_bAddedConnection = true;
        }
        SecureZeroMemory(szUser, ORC_MAX_PATH * sizeof(WCHAR));
        SecureZeroMemory(szPass, ORC_MAX_PATH * sizeof(WCHAR));
    }

    return S_OK;
}

HRESULT
BITSAgent::UploadFile(
    const std::wstring& strLocalName,
    const std::wstring& strRemoteName,
    bool bDeleteWhenCopied,
    const UploadMessage::Ptr& request)
{
    HRESULT hr = E_FAIL;

    if (m_pbcm == nullptr)
        return E_POINTER;

    CComPtr<IBackgroundCopyJob> job;
    GUID JobId;

    // Upload job only support one file transferred at a time
    // we need to create one per file uploaded
    hr = m_pbcm->CreateJob(m_config.JobName.c_str(), BG_JOB_TYPE_UPLOAD, &JobId, &job);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to create job '{}' for background copy [{}]", m_config.JobName, SystemError(hr));
        return hr;
    }

    CComPtr<IBackgroundCopyJob2> job2;
    hr = job->QueryInterface(IID_IBackgroundCopyJob2, reinterpret_cast<void**>(&job2));
    if (!job2)
    {
        Log::Critical(L"Failed to retrieve IID_IBackgroundCopyJob2 interface [{}]", SystemError(hr));
        return hr;
    }

    if (!m_config.UserName.empty() && m_config.AuthScheme != OutputSpec::UploadAuthScheme::Anonymous)
    {
        if (job2 == nullptr)
        {
            Log::Error("Local version of BITS does not support IBackgroundCopyJob2, cannot configure credentials");
        }
        else
        {
            BG_AUTH_CREDENTIALS creds;

            creds.Target = BG_AUTH_TARGET_SERVER;
            creds.Scheme = BG_AUTH_SCHEME_NEGOTIATE;

            switch (m_config.AuthScheme)
            {
                case OutputSpec::UploadAuthScheme::Basic:
                    creds.Scheme = BG_AUTH_SCHEME::BG_AUTH_SCHEME_BASIC;
                    break;
                case OutputSpec::UploadAuthScheme::NTLM:
                    creds.Scheme = BG_AUTH_SCHEME::BG_AUTH_SCHEME_NTLM;
                    break;
                case OutputSpec::UploadAuthScheme::Kerberos:
                    creds.Scheme = BG_AUTH_SCHEME::BG_AUTH_SCHEME_NEGOTIATE;
                    break;
                case OutputSpec::UploadAuthScheme::Negotiate:
                    creds.Scheme = BG_AUTH_SCHEME::BG_AUTH_SCHEME_NEGOTIATE;
                    break;
            }

            WCHAR szUser[ORC_MAX_PATH];
            WCHAR szPass[ORC_MAX_PATH];

            m_config.UserName._Copy_s(szUser, ORC_MAX_PATH, ORC_MAX_PATH);
            m_config.Password._Copy_s(szPass, ORC_MAX_PATH, ORC_MAX_PATH);

            creds.Credentials.Basic.UserName = szUser;
            creds.Credentials.Basic.UserName[m_config.UserName.size()] = L'\0';
            creds.Credentials.Basic.Password = szPass;
            creds.Credentials.Basic.Password[m_config.Password.size()] = L'\0';

            if (FAILED(hr = job2->SetCredentials(&creds)))
            {
                Log::Error("Failed to set credentials on the BITS job [{}]", SystemError(hr));
            }
            else
            {
                Log::Debug("Successfully set the username and password on the BITS job");
            }
        }
    }

    std::wstring strRemotePath = GetRemoteFullPath(strRemoteName);

    if (FAILED(hr = job->AddFile(strRemotePath.c_str(), strLocalName.c_str())))
    {
        Log::Error(L"Failed to add file '{}' to BITS job '{}' [{}]", strLocalName, m_config.JobName, SystemError(hr));
        return hr;
    }

    ULONG ulNotifyFlags = 0L;
    CComPtr<IBackgroundCopyCallback2> pNotify = new CNotifyInterface(
        m_config.JobName, request, m_notificationTarget, bDeleteWhenCopied, strLocalName, strRemotePath);
    if (SUCCEEDED(hr = job->SetNotifyInterface(pNotify)))
    {
        ulNotifyFlags |= BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
        if (FAILED(hr = job->SetNotifyFlags(ulNotifyFlags)))
        {
            Log::Error("Failed to SetNotifyFlags to delete uploaded files [{}]", SystemError(hr));
        }
    }
    else
    {
        Log::Error("Failed to SetNotifyInterface to delete uploaded files [{}]", SystemError(hr));
    }

    if (job2 == nullptr)
    {
        Log::Error("BITS version does not allow for command notification, uploaded file won't be deleted");
    }
    else
    {
        std::wstring strCmdSpec;
        if (FAILED(hr = ExpandFilePath(L"%ComSpec%", strCmdSpec)))
        {
            Log::Error(L"Failed to determine command spec %ComSpec% [{}]", SystemError(hr));
        }
        else
        {
            WCHAR szJobGUID[ORC_MAX_PATH];
            std::wstringstream cmdLine;

            if (!StringFromGUID2(JobId, szJobGUID, ORC_MAX_PATH))
            {
                Log::Error(L"Failed to copy GUID to a string");
                cmdLine << L"\"" << strCmdSpec << L"\" /E:OFF /D /C del \"" << strLocalName << "\"";
            }
            else
            {
                if (bDeleteWhenCopied)
                {
                    cmdLine << L"\"" << strCmdSpec << L"\" /E:OFF /D /C bitsadmin /COMPLETE " << szJobGUID
                            << L" && del \"" << strLocalName << "\"";
                }
                else
                {
                    cmdLine << L"\"" << strCmdSpec << L"\" /E:OFF /D /C bitsadmin /COMPLETE " << szJobGUID;
                }
            }

            if (m_config.bitsMode == OutputSpec::BITSMode::SMB && m_config.bitsDeleteSmbShare.has_value()
                && *m_config.bitsDeleteSmbShare == true)
            {
                Log::Debug(L"Configure NotifyCmd to delete share: {}{}", m_config.ServerName, m_config.RootPath);

                constexpr std::wstring_view forbidden(L";&|()<>*?\"");
                if (boost::algorithm::contains(m_config.ServerName, forbidden)
                    || boost::algorithm::contains(m_config.RootPath, forbidden))
                {
                    Log::Warn("Invalid characters in server name or network path");
                }
                else
                {
                    if (!cmdLine.str().empty())
                    {
                        cmdLine << L" & ";
                    }

                    cmdLine << L"net use /del \"\\\\" << m_config.ServerName << m_config.RootPath << L"\"";

                }
            }

            if (FAILED(hr = job2->SetNotifyCmdLine(strCmdSpec.c_str(), cmdLine.str().c_str())))
            {
                Log::Error(L"Failed to SetNotifyCmdLine to delete uploaded files [{}]", SystemError(hr));
            }
            else
            {
                ulNotifyFlags |= BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
                if (FAILED(hr = job2->SetNotifyFlags(ulNotifyFlags)))
                {
                    Log::Error(L"Failed to SetNotifyFlags to delete uploaded files [{}]", SystemError(hr));
                }
                // We're done, files will be deleted on upload completion
            }
        }
    }

    pNotify.Release();
    m_jobs.emplace_back(JobId, job);

    if (FAILED(hr = job->Resume()))
    {
        Log::Error(L"Failed to start upload transfer [{}]", SystemError(hr));
    }

    return S_OK;
}

HRESULT BITSAgent::IsComplete(bool bReadyToExit, bool& hasFailure)
{
    hasFailure = false;

    if (m_config.Mode == OutputSpec::UploadMode::Asynchronous && bReadyToExit)
        return S_OK;  // If the agent is ready to exit, let it be!!

    for (auto job : m_jobs)
    {
        if (job.m_job)
        {
            BG_JOB_STATE progress = BG_JOB_STATE_QUEUED;

            HRESULT hr = job.m_job->GetState(&progress);
            if (SUCCEEDED(hr))
            {
                switch (progress)
                {
                    case BG_JOB_STATE_QUEUED:
                    case BG_JOB_STATE_CONNECTING:
                    case BG_JOB_STATE_TRANSFERRING:
                    case BG_JOB_STATE_SUSPENDED:
                    case BG_JOB_STATE_TRANSIENT_ERROR:
                        // Those status indicate a job "in progress"
                        return S_FALSE;
                    case BG_JOB_STATE_ERROR: {
                        hasFailure = true;
                    }
                    break;
                    case BG_JOB_STATE_TRANSFERRED:
                    case BG_JOB_STATE_ACKNOWLEDGED:
                    case BG_JOB_STATE_CANCELLED:
                        // Those status indicate a job "complete": either on error or transfered
                    default:
                        break;
                }
            }
            else
            {
                Log::Debug("Failed to retrieve BITS job state [{}]", SystemError(hr));
            }
        }
    }

    // Only getting there if all jobs are done
    if (hasFailure)
    {
        for (auto job : m_jobs)
        {
            if (!job.m_job)
            {
                continue;
            }

            BG_JOB_STATE progress = BG_JOB_STATE_QUEUED;
            if (FAILED(job.m_job->GetState(&progress)) || progress != BG_JOB_STATE_ERROR)
            {
                continue;
            }

            GUID guid = {0};
            job.m_job->GetId(&guid);

            CComPtr<IBackgroundCopyError> error;
            HRESULT hr = job.m_job->GetError(&error);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to retrieve BITS job error (guid: {}) [{}]", guid, SystemError(hr));
                continue;
            }

            LPWSTR description;
            hr = error->GetErrorDescription(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), &description);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to retrieve BITS job error string (guid: {}) [{}]", guid, SystemError(hr));
                continue;
            }

            Log::Error(L"Failed BITS job: {} (guid: {})", description, guid);
        }
    }

    return S_OK;
}

HRESULT BITSAgent::Cancel()
{
    for (auto job : m_jobs)
    {
        if (job.m_job)
        {
            BG_JOB_STATE progress = BG_JOB_STATE_QUEUED;

            if (SUCCEEDED(job.m_job->GetState(&progress)))
            {
                switch (progress)
                {
                    case BG_JOB_STATE_QUEUED:
                    case BG_JOB_STATE_CONNECTING:
                    case BG_JOB_STATE_TRANSFERRING:
                    case BG_JOB_STATE_SUSPENDED:
                    case BG_JOB_STATE_TRANSIENT_ERROR:
                        // Those status indicate a job "in progress"
                        job.m_job->Cancel();
                        break;
                    case BG_JOB_STATE_ERROR:
                    case BG_JOB_STATE_TRANSFERRED:
                    case BG_JOB_STATE_ACKNOWLEDGED:
                    case BG_JOB_STATE_CANCELLED:
                        // Thos status indicate a job "complete": either on error or transfered --> nothing to
                        // cancel here...
                    default:
                        break;
                }
            }
        }
    }
    return S_OK;
}

HRESULT BITSAgent::UnInitialize()
{
    // TODO: Mutualize this code between the copyfileagent & bitsagent
    if (m_bAddedConnection)
    {
        std::wstringstream stream;

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\";

        WNetCancelConnection2(stream.str().c_str(), 0L, TRUE);
    }
    return S_OK;
}

#pragma comment(lib, "winhttp.lib")

HRESULT BITSAgent::CheckFileUploadOverHttp(const std::wstring& strRemoteName, std::optional<DWORD>& fileSize)
{
    HRESULT hr = E_FAIL;

    // Warning!!! to ensure DFIR-Orc does not execute again when upload server is unavailable,
    // we return S_OK in all cases (except HTTP Status 404, in that case, we return S_FALSE)

    std::wstring strRemotePath = GetRemotePath(strRemoteName);

    // Use WinHttpOpen to obtain a session handle.
    HINTERNET hSession = WinHttpOpen(
        L"WinHTTP Program/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed WinHttpOpen on '{}' [{}]", m_config.ServerName, SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hSession)) { WinHttpCloseHandle(hSession); }
    BOOST_SCOPE_EXIT_END;

    // Specify an HTTP server.
    HINTERNET hConnect = WinHttpConnect(hSession, m_config.ServerName.c_str(), INTERNET_DEFAULT_PORT, 0);
    if (hConnect == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed WinHttpConnect on '{}' [{}]", m_config.ServerName, SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hConnect)) { WinHttpCloseHandle(hConnect); }
    BOOST_SCOPE_EXIT_END;

    // Create an HTTP Request handle.
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"HEAD",
        strRemotePath.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        m_config.bitsMode == OutputSpec::BITSMode::HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (hRequest == NULL)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed to open request to {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
        return hr;
    }
    BOOST_SCOPE_EXIT((&hRequest)) { WinHttpCloseHandle(hRequest); }
    BOOST_SCOPE_EXIT_END;

    // Send a Request.
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(
            L"Failed to send status request for {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
        return hr;
    }

    // Place additional code here.
    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed to receive response to {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
        return hr;
    }

    DWORD dwStatusCode = 0L;
    DWORD dwHeaderSize = sizeof(dwStatusCode);
    if (!WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &dwStatusCode,
            &dwHeaderSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed to query status code {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
        return hr;
    }

    if (dwStatusCode == 401)
    {
        DWORD dwSupportedSchemes = 0L;
        DWORD dwFirstScheme = 0L;
        DWORD dwTarget = 0L;

        if (!WinHttpQueryAuthSchemes(hRequest, &dwSupportedSchemes, &dwFirstScheme, &dwTarget))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed to query status code {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
            return hr;
        }

        DWORD dwSelectedScheme = 0L;
        if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_NEGOTIATE)
            dwSelectedScheme = WINHTTP_AUTH_SCHEME_NEGOTIATE;
        else if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_NTLM)
            dwSelectedScheme = WINHTTP_AUTH_SCHEME_NTLM;
        else
        {
            Log::Debug(
                L"No supported authentication scheme available {}/{} [{}]",
                m_config.ServerName,
                strRemotePath,
                SystemError(hr));
            return E_FAIL;
        }

        if (!WinHttpSetCredentials(
                hRequest, dwTarget, dwSelectedScheme, m_config.UserName.c_str(), m_config.Password.c_str(), NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(
                L"Failed WinHttpSetCredentials with {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
            return hr;
        }

        // Send a Request.
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed WinHttpSendRequest on {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
            return hr;
        }

        // Place additional code here.
        if (!WinHttpReceiveResponse(hRequest, NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(
                L"Failed to receive response to {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
            return hr;
        }

        if (!WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &dwStatusCode,
                &dwHeaderSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed to query status code {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
            return hr;
        }
    }

    if (dwStatusCode == 200)
    {
        DWORD dwFileSize = 0;

        dwHeaderSize = sizeof(DWORD);
        if (!WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &dwFileSize,
                &dwHeaderSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(
                L"Failed to query content length {}/{} [{}]", m_config.ServerName, strRemotePath, SystemError(hr));
            return hr;
        }

        fileSize = dwFileSize;
    }
    else if (dwStatusCode == 404)
    {
        return S_FALSE;
    }
    else
    {
        // TODO: add support for generic code (5xx, 4xx, 2xx...)
        Log::Debug("Failed to check remote file using BITS http client (code: {})", dwStatusCode);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT BITSAgent::CheckFileUploadOverSMB(const std::wstring& strRemoteName, std::optional<DWORD>& fileSize)
{
    std::wstring strFullPath = GetRemoteFullPath(strRemoteName);

    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (!GetFileAttributesEx(strFullPath.c_str(), GetFileExInfoStandard, &fileData))
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return S_FALSE;

        return HRESULT_FROM_WIN32(GetLastError());
    }
    else
    {
        if (fileData.nFileSizeHigh > 0)
        {
            fileSize = MAXDWORD;
        }
        else
        {
            fileSize = fileData.nFileSizeLow;
        }
    }

    return S_OK;
}

HRESULT BITSAgent::CheckCompatibleVersion()
{

    CComPtr<IBackgroundCopyManager> pbcm;

    if (SUCCEEDED(pbcm.CoCreateInstance(CLSID_BackgroundCopyManager3_0, NULL, CLSCTX_LOCAL_SERVER)))
        return S_OK;
    if (SUCCEEDED(pbcm.CoCreateInstance(CLSID_BackgroundCopyManager2_5, NULL, CLSCTX_LOCAL_SERVER)))
        return S_OK;
    if (SUCCEEDED(pbcm.CoCreateInstance(CLSID_BackgroundCopyManager2_0, NULL, CLSCTX_LOCAL_SERVER)))
        return S_OK;

    return E_FAIL;
}

std::wstring BITSAgent::GetRemoteFullPath(const std::wstring& strRemoteName)
{
    std::wstringstream stream;
    switch (m_config.bitsMode)
    {
        case OutputSpec::BITSMode::HTTP:
            stream << L"http://" << m_config.ServerName << m_config.RootPath << L"/" << strRemoteName;
            break;
        case OutputSpec::BITSMode::HTTPS:
            stream << L"https://" << m_config.ServerName << m_config.RootPath << L"/" << strRemoteName;
            break;
        case OutputSpec::BITSMode::SMB:
            stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\" << strRemoteName;
            break;
    }
    return stream.str();
}

std::wstring BITSAgent::GetRemotePath(const std::wstring& strRemoteName)
{
    std::wstringstream stream;
    switch (m_config.bitsMode)
    {
        case OutputSpec::BITSMode::HTTP:
            stream << m_config.RootPath << L"/" << strRemoteName;
            break;
        case OutputSpec::BITSMode::HTTPS:
            stream << m_config.RootPath << L"/" << strRemoteName;
            break;
        case OutputSpec::BITSMode::SMB:
            stream << m_config.RootPath << L"\\" << strRemoteName;
            break;
    }
    return stream.str();
}

HRESULT BITSAgent::CheckFileUpload(const std::wstring& strRemoteName, std::optional<DWORD>& fileSize)
{
    HRESULT hr = E_FAIL;

    switch (m_config.bitsMode)
    {
        case OutputSpec::BITSMode::HTTP:
        case OutputSpec::BITSMode::HTTPS:
            hr = CheckFileUploadOverHttp(strRemoteName, fileSize);
            if (FAILED(hr))
            {
                Log::Debug("Failed to check file status over http [{}]", SystemError(hr));
                return hr;
            }
            break;
        case OutputSpec::BITSMode::SMB:
            hr = CheckFileUploadOverSMB(strRemoteName, fileSize);
            if (FAILED(hr))
            {
                Log::Debug("Failed to check file status over smb [{}]", SystemError(hr));
                return hr;
            }
            break;
        default:
            Log::Critical(L"Invalid BITS Mode {}", static_cast<size_t>(m_config.bitsMode));
            break;
    }

    return hr;
}

BITSAgent::~BITSAgent()
{
    m_pbcm = nullptr;
}

//
// Notification interface
//
constexpr auto TWO_GB = 2LU * 1024 * 1024 * 1024;  // 2GB

HRESULT CNotifyInterface::QueryInterface(REFIID riid, LPVOID* ppvObj)
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

ULONG CNotifyInterface::AddRef()
{
    return InterlockedIncrement(&m_lRefCount);
}

ULONG CNotifyInterface::Release()
{
    ULONG ulCount = InterlockedDecrement(&m_lRefCount);

    if (0 == ulCount)
    {
        delete this;
    }

    return ulCount;
}

HRESULT CNotifyInterface::JobTransferred(IBackgroundCopyJob* pJob)
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
    }

    if (m_bDeleteWhenUploaded && !m_source.empty())
    {
        if (!DeleteFile(m_source.c_str()))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                auto notify = UploadNotification::MakeFailureNotification(
                    m_request,
                    UploadNotification::Type::UploadComplete,
                    m_source,
                    m_destination,
                    HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to delete uploaded file on completion");
                Concurrency::send(m_pNotificationTarget, notify);
            }
        }
    }
    auto notify = UploadNotification::MakeSuccessNotification(
        m_request, UploadNotification::Type::UploadComplete, m_source, m_destination);
    Concurrency::send(m_pNotificationTarget, notify);

    // If you do not return S_OK, BITS continues to call this callback.
    return S_OK;
}

HRESULT CNotifyInterface::JobError(IBackgroundCopyJob* pJob, IBackgroundCopyError* pError)
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
    {
        Log::Debug("Failed to retrieve BITS job error [{}]", SystemError(hr));

        // BEWARE: this was a missing code and return statement. Before it continued to last 'return S_OK' without
        // any notification.
        auto notify = UploadNotification::MakeFailureNotification(
            m_request, UploadNotification::Type::UploadComplete, m_source, m_destination, E_FAIL, L"Unknown job error");
        Concurrency::send(m_pNotificationTarget, notify);
        return S_OK;
    }

    // If the proxy or server does not support the Content-Range header or if
    // antivirus software removes the range requests, BITS returns BG_E_INSUFFICIENT_RANGE_SUPPORT.
    // This implementation tries to switch the job to foreground priority, so
    // the content has a better chance of being successfully downloaded.
    if (BG_E_INSUFFICIENT_RANGE_SUPPORT == ErrorCode)
    {
        CComPtr<IBackgroundCopyFile> pFile;

        hr = pError->GetFile(&pFile);
        hr = pFile->GetProgress(&Progress);
        if (BG_SIZE_UNKNOWN == Progress.BytesTotal)
        {
            // The content is dynamic, do not change priority. Handle as an error.
            auto notify = UploadNotification::MakeFailureNotification(
                m_request,
                UploadNotification::Type::UploadComplete,
                m_source,
                m_destination,
                HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to set ranges for uploaded file");
            Concurrency::send(m_pNotificationTarget, notify);
        }
        else if (Progress.BytesTotal > TWO_GB)
        {
            // BITS does not use range requests if the content is less than 2 GB.
            // However, if the content is greater than 2 GB, BITS
            // uses 2 GB ranges to download the file, so switching to foreground
            // priority will not help.
            auto notify = UploadNotification::MakeFailureNotification(
                m_request,
                UploadNotification::Type::UploadComplete,
                m_source,
                m_destination,
                HRESULT_FROM_WIN32(GetLastError()),
                L"File is more than 2GB, resume fails");
            Concurrency::send(m_pNotificationTarget, notify);
        }
        else
        {
            Log::Debug(L"Background upload failed, switching to foreground priority");
            hr = pJob->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
            hr = pJob->Resume();
            IsError = FALSE;
        }
    }

    if (TRUE == IsError)
    {
        hr = pError->GetErrorDescription(LANGIDFROMLCID(GetThreadLocale()), &pszErrorDescription);
        if (FAILED(hr))
        {
            Log::Debug("Failed to retrieve error description [{}]", hr);
        }

        // Do something with the job name and description.
        auto notify = UploadNotification::MakeFailureNotification(
            m_request,
            UploadNotification::Type::UploadComplete,
            m_source,
            m_destination,
            ErrorCode,
            pszErrorDescription ? pszErrorDescription : L"Unknown error");
        Concurrency::send(m_pNotificationTarget, notify);

        if (pszErrorDescription)
        {
            CoTaskMemFree(pszErrorDescription);
        }
    }

    // If you do not return S_OK, BITS continues to call this callback.
    return S_OK;
}

HRESULT CNotifyInterface::JobModification(IBackgroundCopyJob* pJob, DWORD dwReserved)
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

HRESULT CNotifyInterface::FileTransferred(IBackgroundCopyJob* pJob, IBackgroundCopyFile* pFile)
{
    DBG_UNREFERENCED_PARAMETER(pJob);
    HRESULT hr = S_OK;
    CComPtr<IBackgroundCopyFile3> pFile3 = NULL;

    hr = pFile->QueryInterface(__uuidof(IBackgroundCopyFile3), (void**)&pFile3);
    if (SUCCEEDED(hr))
    {
        // Add code to validate downloaded content and set IsValid.

        hr = pFile3->SetValidationState(TRUE);
        if (FAILED(hr))
        {
            // Handle error
        }
    }
    else
    {
        Log::Debug("Failed handling FileTransferred notification [{}]", SystemError(hr));
    }

    return S_OK;
}
