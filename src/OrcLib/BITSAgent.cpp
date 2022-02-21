//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "BITSAgent.h"

#include "LogFileWriter.h"

#include "ParameterCheck.h"

#include <Bits.h>
#include <Winhttp.h>

#include <sstream>
#include <regex>
#include <boost/scope_exit.hpp>

using namespace std;
using namespace Orc;

#pragma comment(lib, "BITS.lib")

class CNotifyInterface : public IBackgroundCopyCallback2
{
public:
    // Constructor, Destructor
    CNotifyInterface(
        logger pLog,
        std::wstring strJobName,
        UploadNotification::ITarget& pNotificationTarget,
        bool bDeleteWhenUploaded = false,
        const std::wstring& strFile = L"")
        : _L_(std::move(pLog))
        , m_strJobName(strJobName)
        , m_bDeleteWhenUploaded(bDeleteWhenUploaded)
        , m_strUploadedFile(strFile)
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
    logger _L_;

    LONG m_lRefCount = 0L;
    LONG m_PendingJobModificationCount = 0L;

    std::wstring m_strJobName;
    std::wstring m_strUploadedFile;
    bool m_bDeleteWhenUploaded = false;

    UploadNotification::ITarget& m_pNotificationTarget;
};

HRESULT BITSAgent::Initialize()
{
    HRESULT hr = E_FAIL;

    if (m_config.Method != OutputSpec::UploadMethod::BITS)
        return E_UNEXPECTED;

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
                    log::Error(_L_, hr, L"Failed to create instance of background copy manager 2.0\r\n");
                    return hr;
                }
            }
            else
            {
                log::Error(_L_, hr, L"Failed to create instance of background copy manager 2.5\r\n");
                return hr;
            }
        }
    }
    else
        return hr;


    //TODO: Mutualize this code between the copyfileagent & bitsagent
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

        wstringstream stream;
        WCHAR szUNC[MAX_PATH];
        ZeroMemory(szUNC, MAX_PATH * sizeof(WCHAR));

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath;
        stream.str()._Copy_s(szUNC, MAX_PATH, MAX_PATH);
        nr.lpRemoteName = szUNC;

        WCHAR szUser[MAX_PATH];
        WCHAR szPass[MAX_PATH];
        ZeroMemory(szUser, MAX_PATH * sizeof(WCHAR));
        ZeroMemory(szPass, MAX_PATH * sizeof(WCHAR));
        m_config.UserName._Copy_s(szUser, MAX_PATH, MAX_PATH);
        szUser[m_config.UserName.size()] = L'\0';
        m_config.Password._Copy_s(szPass, MAX_PATH, MAX_PATH);
        szPass[m_config.Password.size()] = L'\0';

        DWORD dwRet = 0;

        if ((dwRet = WNetAddConnection2(&nr, szPass, szUser, CONNECT_TEMPORARY)) != NO_ERROR)
        {
            log::Error(_L_, HRESULT_FROM_WIN32(dwRet), L"Failed to add a connection to %s\r\n", szUNC);
        }
        else
        {
            //TODO: remove this diag
            log::Info(_L_, L"Added a connection to %s\r\n", szUNC);
            m_bAddedConnection = true;
        }
        SecureZeroMemory(szUser, MAX_PATH * sizeof(WCHAR));
        SecureZeroMemory(szPass, MAX_PATH * sizeof(WCHAR));
    }

    return S_OK;
}

HRESULT
BITSAgent::UploadFile(const std::wstring& strLocalName, const std::wstring& strRemoteName, bool bDeleteWhenCopied)
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
        log::Error(_L_, hr, L"Failed to create job %s for background copy\r\n", m_config.JobName.c_str());
        return hr;
    }

    CComQIPtr<IBackgroundCopyJob2> job2 = job;

    if (!m_config.UserName.empty() && m_config.AuthScheme != OutputSpec::UploadAuthScheme::Anonymous)
    {
        if (job2 == nullptr)
        {
            log::Error(
                _L_,
                E_NOINTERFACE,
                L"Local version of BITS does not support IBackgroundCopyJob2, cannot configure credentials\r\n");
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

            WCHAR szUser[MAX_PATH];
            WCHAR szPass[MAX_PATH];

            m_config.UserName._Copy_s(szUser, MAX_PATH, MAX_PATH);
            m_config.Password._Copy_s(szPass, MAX_PATH, MAX_PATH);

            creds.Credentials.Basic.UserName = szUser;
            creds.Credentials.Basic.UserName[m_config.UserName.size()] = L'\0';
            creds.Credentials.Basic.Password = szPass;
            creds.Credentials.Basic.Password[m_config.Password.size()] = L'\0';

            if (FAILED(hr = job2->SetCredentials(&creds)))
            {
                log::Error(_L_, hr, L"Failed to set credentials on the BITS job\r\n");
            }
            else
            {
                log::Verbose(_L_, L"Successfully set the username and password on the BITS job\r\n");
            }
        }
    }

    wstring strRemotePath = GetRemoteFullPath(strRemoteName);

    if (FAILED(hr = job->AddFile(strRemotePath.c_str(), strLocalName.c_str())))
    {
        log::Error(
            _L_, hr, L"Failed to add file %s to BITS job %s\r\n", strLocalName.c_str(), m_config.JobName.c_str());
        return hr;
    }

    ULONG ulNotifyFlags = 0L;
    CComPtr<IBackgroundCopyCallback2> pNotify;
    if (bDeleteWhenCopied)
    {
        pNotify = new CNotifyInterface(_L_, m_config.JobName, m_notificationTarget, true, strLocalName);
    }
    else
    {
        pNotify = new CNotifyInterface(_L_, m_config.JobName, m_notificationTarget, false, L"");
    }

    if (SUCCEEDED(hr = job->SetNotifyInterface(pNotify)))
    {
        ulNotifyFlags |= BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
        if (FAILED(hr = job->SetNotifyFlags(ulNotifyFlags)))
        {
            log::Error(_L_, hr, L"Failed to SetNotifyFlags to delete uploaded files\r\n");
        }
    }
    else
    {
        log::Error(_L_, hr, L"Failed to SetNotifyInterface to delete uploaded files\r\n");
    }

    if (job2 == nullptr)
    {
        log::Error(
            _L_,
            E_NOTIMPL,
            L"BITS version does not allow for command notification, uploaded file won't be deleted\r\n");
    }
    else
    {
        std::wstring strCmdSpec;
        if (FAILED(hr = ExpandFilePath(L"%ComSpec%", strCmdSpec)))
        {
            log::Error(_L_, hr, L"Failed to determine command spec (%ComSpec%)\r\n");
        }
        else
        {
            WCHAR szJobGUID[MAX_PATH];
            wstringstream cmdLine;

            if (!StringFromGUID2(JobId, szJobGUID, MAX_PATH))
            {
                log::Error(_L_, E_FAIL, L"Failed to copy GUID to a string\r\n");
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

            if (FAILED(hr = job2->SetNotifyCmdLine(strCmdSpec.c_str(), cmdLine.str().c_str())))
            {
                log::Error(_L_, hr, L"Failed to SetNotifyCmdLine to delete uploaded files\r\n");
            }
            else
            {
                ulNotifyFlags |= BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR;
                if (FAILED(hr = job2->SetNotifyFlags(ulNotifyFlags)))
                {
                    log::Error(_L_, hr, L"Failed to SetNotifyFlags to delete uploaded files\r\n");
                }
                // We're done, files will be deleted on upload completion
            }
        }
    }

    pNotify.Release();
    m_jobs.emplace_back(JobId, job);

    if (FAILED(hr = job->Resume()))
    {
        log::Error(_L_, hr, L"Failed to start upload transfer\r\n");
    }

    return S_OK;
}

HRESULT BITSAgent::IsComplete(bool bReadyToExit)
{
    if (m_config.Mode == OutputSpec::UploadMode::Asynchronous && bReadyToExit)
        return S_OK;  // If the agent is ready to exit, let it be!!

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
                        return S_FALSE;
                    case BG_JOB_STATE_ERROR:
                    case BG_JOB_STATE_TRANSFERRED:
                    case BG_JOB_STATE_ACKNOWLEDGED:
                    case BG_JOB_STATE_CANCELLED:
                        // Thos status indicate a job "complete": either on error or transfered
                    default:
                        break;
                }
            }
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
                        // Thos status indicate a job "complete": either on error or transfered --> nothing to cancel
                        // here...
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
        wstringstream stream;

        stream << L"\\\\" << m_config.ServerName << m_config.RootPath << L"\\";

        WNetCancelConnection2(stream.str().c_str(), 0L, TRUE);
    }
    return S_OK;
}

#pragma comment(lib, "winhttp.lib")

HRESULT BITSAgent::CheckFileUploadOverHttp(const std::wstring& strRemoteName, PDWORD pdwFileSize)
{
    HRESULT hr = E_FAIL;

    if (pdwFileSize)
        *pdwFileSize = MAXDWORD;

    // Warning!!! to ensure DFIR-Orc does not execute again when upload server is unavailable,
    // we return S_OK in all cases (except HTTP Status 404, in that case, we return S_FALSE)

    wstring strRemotePath = GetRemotePath(strRemoteName);

    // Use WinHttpOpen to obtain a session handle.
    HINTERNET hSession = WinHttpOpen(
        L"WinHTTP Program/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL)
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to open %s\r\n", m_config.ServerName.c_str());
        return hr;
    }
    BOOST_SCOPE_EXIT((&hSession)) { WinHttpCloseHandle(hSession); }
    BOOST_SCOPE_EXIT_END;

    // Specify an HTTP server.
    HINTERNET hConnect = WinHttpConnect(hSession, m_config.ServerName.c_str(), INTERNET_DEFAULT_PORT, 0);
    if (hConnect == NULL)
    {
        log::Error(
            _L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to connect to %s\r\n", m_config.ServerName.c_str());
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
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to open request to %s/%s\r\n",
            m_config.ServerName.c_str(),
            strRemotePath.c_str());
        return hr;
    }
    BOOST_SCOPE_EXIT((&hRequest)) { WinHttpCloseHandle(hRequest); }
    BOOST_SCOPE_EXIT_END;

    // Send a Request.
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to send status request for %s/%s\r\n",
            m_config.ServerName.c_str(),
            strRemotePath.c_str());
        return hr;
    }

    // Place additional code here.
    if (!WinHttpReceiveResponse(hRequest, NULL))
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to receive response to %s/%s\r\n",
            m_config.ServerName.c_str(),
            strRemotePath.c_str());
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
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to query status code %s/%s\r\n",
            m_config.ServerName.c_str(),
            strRemotePath.c_str());
        return hr;
    }

    if (dwStatusCode == 401)
    {
        DWORD dwSupportedSchemes = 0L;
        DWORD dwFirstScheme = 0L;
        DWORD dwTarget = 0L;

        if (!WinHttpQueryAuthSchemes(hRequest, &dwSupportedSchemes, &dwFirstScheme, &dwTarget))
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to query status code %s/%s\r\n",
                m_config.ServerName.c_str(),
                strRemotePath.c_str());
            return hr;
        }

        DWORD dwSelectedScheme = 0L;
        if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_NEGOTIATE)
            dwSelectedScheme = WINHTTP_AUTH_SCHEME_NEGOTIATE;
        else if (dwSupportedSchemes & WINHTTP_AUTH_SCHEME_NTLM)
            dwSelectedScheme = WINHTTP_AUTH_SCHEME_NTLM;
        else
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"No supported authentication scheme available %s/%s\r\n",
                m_config.ServerName.c_str(),
                strRemotePath.c_str());
            return hr;
        }

        if (!WinHttpSetCredentials(
                hRequest, dwTarget, dwSelectedScheme, m_config.UserName.c_str(), m_config.Password.c_str(), NULL))
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to authenticate to %s/%s\r\n",
                m_config.ServerName.c_str(),
                strRemotePath.c_str());
            return hr;
        }

        // Send a Request.
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to send status request for %s/%s\r\n",
                m_config.ServerName.c_str(),
                strRemotePath.c_str());
            return hr;
        }

        // Place additional code here.
        if (!WinHttpReceiveResponse(hRequest, NULL))
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to receive response to %s/%s\r\n",
                m_config.ServerName.c_str(),
                strRemotePath.c_str());
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
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to query status code %s/%s\r\n",
                m_config.ServerName.c_str(),
                strRemotePath.c_str());
            return hr;
        }
    }

    if (dwStatusCode == 200)
    {
        if (pdwFileSize)
        {
            dwHeaderSize = sizeof(DWORD);
            if (!WinHttpQueryHeaders(
                    hRequest,
                    WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    pdwFileSize,
                    &dwHeaderSize,
                    WINHTTP_NO_HEADER_INDEX))
            {
                log::Error(
                    _L_,
                    hr = HRESULT_FROM_WIN32(GetLastError()),
                    L"Failed to query content length %s/%s\r\n",
                    m_config.ServerName.c_str(),
                    strRemotePath.c_str());
                return hr;
            }
        }
    }
    else if (dwStatusCode == 404)
    {
        if (pdwFileSize)
            *pdwFileSize = 0L;
        return S_FALSE;
    }
    return S_OK;
}

HRESULT BITSAgent::CheckFileUploadOverSMB(const std::wstring& strRemoteName, PDWORD pdwFileSize)
{
    if (pdwFileSize)
        *pdwFileSize = 0L;

    wstring strFullPath = GetRemoteFullPath(strRemoteName);

    if (pdwFileSize)
    {
        *pdwFileSize = 0L;
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
                *pdwFileSize = MAXDWORD;
            }
            else
            {
                *pdwFileSize = fileData.nFileSizeLow;
            }
        }
    }
    else
    {
        if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(strFullPath.c_str()))
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return S_FALSE;
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
    wstringstream stream;
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
    wstringstream stream;
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

HRESULT BITSAgent::CheckFileUpload(const std::wstring& strRemoteName, PDWORD pdwFileSize)
{
    HRESULT hr = E_FAIL;

    switch (m_config.bitsMode)
    {
        case OutputSpec::BITSMode::HTTP:
        case OutputSpec::BITSMode::HTTPS:
            return CheckFileUploadOverHttp(strRemoteName, pdwFileSize);
        case OutputSpec::BITSMode::SMB:
            return CheckFileUploadOverSMB(strRemoteName, pdwFileSize);
    }
    return S_OK;
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

    if (m_bDeleteWhenUploaded && !m_strUploadedFile.empty())
    {
        if (!DeleteFile(m_strUploadedFile.c_str()))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                auto notify = UploadNotification::MakeFailureNotification(
                    UploadNotification::Type::UploadComplete,
                    HRESULT_FROM_WIN32(GetLastError()),
                    m_strUploadedFile,
                    L"Failed to delete uploaded file on completion");
                Concurrency::send(m_pNotificationTarget, notify);
            }
        }
    }
    auto notify =
        UploadNotification::MakeSuccessNotification(UploadNotification::Type::UploadComplete, m_strUploadedFile);
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
                UploadNotification::Type::UploadComplete,
                HRESULT_FROM_WIN32(GetLastError()),
                m_strJobName,
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
                UploadNotification::Type::UploadComplete,
                HRESULT_FROM_WIN32(GetLastError()),
                m_strJobName,
                L"File is more than 2GB, resume fails");
            Concurrency::send(m_pNotificationTarget, notify);
        }
        else
        {
            log::Verbose(_L_, L"Background upload failed, switching to foreground priority\r\n");
            hr = pJob->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
            hr = pJob->Resume();
            IsError = FALSE;
        }
    }

    if (TRUE == IsError)
    {
        hr = pJob->GetDisplayName(&pszJobName);
        hr = pError->GetErrorDescription(LANGIDFROMLCID(GetThreadLocale()), &pszErrorDescription);

        if (pszJobName && pszErrorDescription)
        {
            // Do something with the job name and description.
            auto notify = UploadNotification::MakeFailureNotification(
                UploadNotification::Type::UploadComplete,
                HRESULT_FROM_WIN32(GetLastError()),
                m_strJobName,
                pszErrorDescription);
            Concurrency::send(m_pNotificationTarget, notify);
        }

        CoTaskMemFree(pszJobName);
        CoTaskMemFree(pszErrorDescription);
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

    return S_OK;
}
