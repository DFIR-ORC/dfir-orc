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

#include "LogFileWriter.h"

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
            case UploadMessage::UploadFile:
            {
                if (bIsReadyToBeDone)
                    break;  // no new upload is accepted

                UploadNotification::Notification notification;

                if (FAILED(hr = UploadFile(request->LocalName(), request->RemoteName(), request->GetDeleteWhenDone())))
                {
                    notification = UploadNotification::MakeFailureNotification(
                        UploadNotification::FileAddition,
                        hr,
                        request->LocalName(),
                        L"Failed to upload file to destination server");
                }
                else
                {
                    notification = UploadNotification::MakeSuccessNotification(
                        UploadNotification::FileAddition, request->LocalName());
                }

                if (notification)
                    SendResult(notification);
            }
            break;
            case UploadMessage::UploadDirectory:
            {
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
                        UploadNotification::DirectoryAddition,
                        HRESULT_FROM_WIN32(GetLastError()),
                        request->LocalName(),
                        L"FindFile failed");
                    if (notification)
                        SendResult(notification);
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

                            if (FAILED(hr = UploadFile(strFileName, strRemoteName, request->GetDeleteWhenDone())))
                            {
                                notification = UploadNotification::MakeFailureNotification(
                                    UploadNotification::FileAddition,
                                    hr,
                                    strFileName,
                                    L"Failed to upload file to destination server");
                            }
                            else
                            {
                                notification = UploadNotification::MakeSuccessNotification(
                                    UploadNotification::FileAddition, strFileName);
                            }

                            if (notification)
                                SendResult(notification);
                        }
                        else
                        {
                            // Directory?
                        }
                    } while (FindNextFile(hFind, &ffd) != 0);

                    if (!FindClose(hFind))
                    {
                        log::Warning(
                            _L_,
                            HRESULT_FROM_WIN32(GetLastError()),
                            L"Failed to close FindFile while adding directory to copy\r\n");
                    }
                }
            }
            break;
            case UploadMessage::UploadStream:
            {
                if (bIsReadyToBeDone)
                    break;  // no new upload is accepted

                UploadNotification::Notification notification;

                if (notification)
                    SendResult(notification);
            }
            break;
            case UploadMessage::RefreshJobStatus:
            {
                if (IsComplete(bIsReadyToBeDone) == S_OK)
                {
                    UploadNotification::Notification notification;
                    if (FAILED(hr = UnInitialize()))
                    {
                        notification = UploadNotification::MakeFailureNotification(
                            UploadNotification::JobComplete,
                            hr,
                            request->JobName(),
                            L"Failed to unitialize upload agent");
                    }
                    else
                    {
                        notification = UploadNotification::MakeSuccessNotification(
                            UploadNotification::JobComplete, request->JobName());
                    }
                    if (notification)
                        SendResult(notification);
                    done();
                    return;
                }
            }
            break;
            case UploadMessage::Cancel:
            {
                UploadNotification::Notification notification;
                if (FAILED(Cancel()))
                {
                    notification = UploadNotification::MakeFailureNotification(
                        UploadNotification::Cancelled, hr, request->JobName(), L"Failed to cancel current BITS jobs");
                }
                else
                {
                    notification =
                        UploadNotification::MakeSuccessNotification(UploadNotification::Cancelled, request->JobName());
                }
            }
            break;
            case UploadMessage::Complete:
            {
                if (bIsReadyToBeDone)
                    break;  // ready already

                bIsReadyToBeDone = true;

                m_RefreshTimer = std::make_unique<Concurrency::timer<UploadMessage::Message>>(
                    1000, UploadMessage::MakeRefreshJobStatusRequest(), &m_requestTarget, true);
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
    const logger& pLog,
    const OutputSpec::Upload& uploadSpec,
    UploadMessage::ISource& msgSource,
    UploadMessage::ITarget& msgTarget,
    UploadNotification::ITarget& target)
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<UploadAgent> retval;

    switch (uploadSpec.Method)
    {
        case OutputSpec::UploadMethod::FileCopy:
        {
            retval = std::make_shared<CopyFileAgent>(pLog, msgSource, msgTarget, target);
            if (!retval)
                return nullptr;
            if (FAILED(hr = retval->SetConfiguration(uploadSpec)))
            {
                log::Error(pLog, hr, L"Failed to configure CopyFileAgent\r\n");
                return nullptr;
            }
        }
        break;
        case OutputSpec::UploadMethod::BITS:
        {
            retval = std::make_shared<BITSAgent>(pLog, msgSource, msgTarget, target);
            if (!retval)
                return nullptr;

            if (SUCCEEDED(BITSAgent::CheckCompatibleVersion()))
            {
                if (FAILED(hr = retval->SetConfiguration(uploadSpec)))
                {
                    log::Error(pLog, hr, L"Failed to configure CopyFileAgent\r\n");
                    return nullptr;
                }
            }
            else if (uploadSpec.bitsMode == OutputSpec::BITSMode::SMB)
            {
                OutputSpec::Upload newConfig = uploadSpec;
                newConfig.Method = OutputSpec::UploadMethod::FileCopy;
                if (FAILED(hr = retval->SetConfiguration(newConfig)))
                {
                    log::Error(pLog, hr, L"Failed to configure CopyFileAgent\r\n");
                    return nullptr;
                }
            }
            else
            {
                log::Error(pLog, hr, L"Failed to configure BITS Agent : incompatible BITS version installed\r\n");
                return nullptr;
            }
        }
        break;
        default:
            return nullptr;
    }

    if (FAILED(hr = retval->Initialize()))
    {
        log::Error(pLog, hr, L"Failed to initialize CopyFileAgent\r\n");
        return nullptr;
    }
    return retval;
}
