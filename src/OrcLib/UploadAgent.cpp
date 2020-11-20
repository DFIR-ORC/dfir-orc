//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "UploadAgent.h"

#include "Robustness.h"

using namespace Orc;

void UploadAgent::run()
{
    HRESULT hr = E_FAIL;
    bool bIsReadyToBeDone = false;

    UploadMessage::Message request = GetRequest();

    while (request)
    {
        switch (request->GetRequest())
        {
            case UploadMessage::UploadFile: {
                if (bIsReadyToBeDone)
                    break;  // no new upload is accepted

                UploadNotification::Notification notification;

                hr = UploadFile(request->LocalName(), request->RemoteName(), request->GetDeleteWhenDone(), request);
                if (FAILED(hr))
                {
                    notification = UploadNotification::MakeFailureNotification(
                        request,
                        UploadNotification::FileAddition,
                        request->LocalName(),
                        request->RemoteName(),
                        hr,
                        L"Failed to upload file to destination server");
                }
                else
                {
                    notification = UploadNotification::MakeSuccessNotification(
                        request, UploadNotification::FileAddition, request->LocalName(), request->RemoteName());

                    if (notification)
                    {
                        notification->SetFileSize(request->LocalName());
                    }
                }

                if (notification)
                {
                    SendResult(notification);
                }
            }
            break;
            case UploadMessage::UploadDirectory: {
                if (bIsReadyToBeDone)
                    break;  // no new upload is accepted

                WIN32_FIND_DATA ffd;

                std::wstring strSearchString(request->LocalName().c_str());

                if (request->Pattern().empty())
                    strSearchString.append(L"\\*");
                else
                {
                    strSearchString.append(L"\\");
                    strSearchString.append(request->Pattern());
                }

                HANDLE hFind = FindFirstFile(strSearchString.c_str(), &ffd);

                std::vector<std::wstring> addedFiles;

                if (INVALID_HANDLE_VALUE == hFind)
                {
                    auto notification = UploadNotification::MakeFailureNotification(
                        request,
                        UploadNotification::DirectoryAddition,
                        request->LocalName(),
                        request->RemoteName(),
                        HRESULT_FROM_WIN32(GetLastError()),
                        L"FindFile failed");
                    if (notification)
                    {
                        SendResult(notification);
                    }
                }
                else
                {
                    // List all the files in the directory with some info about them.

                    do
                    {
                        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            std::wstring strFileName = request->LocalName();
                            strFileName.append(L"\\");
                            strFileName.append(ffd.cFileName);

                            std::wstring strRemoteName;
                            strRemoteName.assign(request->RemoteName());
                            strRemoteName.append(L"\\");
                            strRemoteName.append(ffd.cFileName);
                            UploadNotification::Notification notification;

                            hr = UploadFile(strFileName, strRemoteName, request->GetDeleteWhenDone(), request);
                            if (FAILED(hr))
                            {
                                notification = UploadNotification::MakeFailureNotification(
                                    request,
                                    UploadNotification::FileAddition,
                                    strFileName,
                                    strRemoteName,
                                    hr,
                                    L"Failed to upload file to destination server");
                            }
                            else
                            {
                                notification = UploadNotification::MakeSuccessNotification(
                                    request, UploadNotification::FileAddition, strFileName, strRemoteName);

                                if (notification)
                                {
                                    notification->SetFileSize(strFileName);
                                }
                            }

                            if (notification)
                            {
                                SendResult(notification);
                            }

                        }
                        else
                        {
                            // Directory?
                        }
                    } while (FindNextFile(hFind, &ffd) != 0);

                    if (!FindClose(hFind))
                    {
                        hr = HRESULT_FROM_WIN32(GetLastError());
                        Log::Warn("Failed to close FindFile while adding directory to copy [{}]", SystemError(hr));
                    }
                }
            }
            break;
            case UploadMessage::UploadStream: {
                if (bIsReadyToBeDone)
                {
                    break;  // no new upload is accepted
                }

                UploadNotification::Notification notification;

                if (notification)
                {
                    SendResult(notification);
                }
            }
            break;
            case UploadMessage::RefreshJobStatus: {
                if (IsComplete(bIsReadyToBeDone) == S_OK)
                {
                    UploadNotification::Notification notification;
                    hr = UnInitialize();
                    if (FAILED(hr))
                    {
                        notification = UploadNotification::MakeFailureNotification(
                            request,
                            UploadNotification::JobComplete,
                            request->LocalName(),
                            request->RemoteName(),
                            hr,
                            L"Failed to uninitialize upload agent");
                    }
                    else
                    {
                        notification = UploadNotification::MakeSuccessNotification(
                            request, UploadNotification::JobComplete, request->LocalName(), request->RemoteName());
                    }

                    if (notification)
                    {
                        SendResult(notification);
                    }

                    done();
                    return;
                }
            }
            break;
            case UploadMessage::Cancel: {
                UploadNotification::Notification notification;
                if (FAILED(Cancel()))
                {
                    notification = UploadNotification::MakeFailureNotification(
                        request,
                        UploadNotification::Cancelled,
                        request->LocalName(),
                        request->RemoteName(),
                        hr,
                        L"Failed to cancel current BITS jobs");
                }
                else
                {
                    notification = UploadNotification::MakeSuccessNotification(
                        request, UploadNotification::Cancelled, request->LocalName(), request->RemoteName());
                }
            }
            break;
            case UploadMessage::Complete: {
                if (bIsReadyToBeDone)
                {
                    break;  // ready already
                }

                bIsReadyToBeDone = true;

                auto refreshRequest = UploadMessage::MakeRefreshJobStatusRequest();
                m_RefreshTimer = std::make_unique<Concurrency::timer<UploadMessage::Message>>(
                    1000, refreshRequest, &m_requestTarget, true);
                m_RefreshTimer->start();
            }
            break;
        }
        request = GetRequest();
    }
    return;
}

#include "CopyFileAgent.h"
#include "BITSAgent.h"

std::shared_ptr<UploadAgent> UploadAgent::CreateUploadAgent(
    const OutputSpec::Upload& uploadSpec,
    UploadMessage::ISource& msgSource,
    UploadMessage::ITarget& msgTarget,
    UploadNotification::ITarget& target)
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<UploadAgent> retval;

    switch (uploadSpec.Method)
    {
        case OutputSpec::UploadMethod::FileCopy: {
            retval = std::make_shared<CopyFileAgent>(msgSource, msgTarget, target);
            if (!retval)
                return nullptr;
            if (FAILED(hr = retval->SetConfiguration(uploadSpec)))
            {
                Log::Error(L"Failed to configure CopyFileAgent [{}]", SystemError(hr));
                return nullptr;
            }
        }
        break;
        case OutputSpec::UploadMethod::BITS: {
            retval = std::make_shared<BITSAgent>(msgSource, msgTarget, target);
            if (!retval)
                return nullptr;

            if (SUCCEEDED(BITSAgent::CheckCompatibleVersion()))
            {
                if (FAILED(hr = retval->SetConfiguration(uploadSpec)))
                {
                    Log::Error(L"Failed to configure CopyFileAgent");
                    return nullptr;
                }
            }
            else if (uploadSpec.bitsMode == OutputSpec::BITSMode::SMB)
            {
                OutputSpec::Upload newConfig = uploadSpec;
                newConfig.Method = OutputSpec::UploadMethod::FileCopy;
                if (FAILED(hr = retval->SetConfiguration(newConfig)))
                {
                    Log::Error(L"Failed to configure CopyFileAgent [{}]", SystemError(hr));
                    return nullptr;
                }
            }
            else
            {
                Log::Error(L"Failed to configure BITS Agent: incompatible BITS version installed");
                return nullptr;
            }
        }
        break;
        default:
            return nullptr;
    }

    if (FAILED(hr = retval->Initialize()))
    {
        Log::Error(L"Failed to initialize CopyFileAgent [{}]", SystemError(hr));
        return nullptr;
    }
    return retval;
}
