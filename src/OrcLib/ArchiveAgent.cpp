//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ArchiveAgent.h"

#include <filesystem>

#include "Robustness.h"

using namespace std;

using namespace Orc;

namespace {

void SetFileSize(const OrcArchive::ArchiveItem& item, ArchiveNotification& notification)
{
    // ArchiveItem's size is initialized with ByteStream::GetSize() which will return -1 if unsupported
    if (item.Size && item.Size != -1)
    {
        notification.SetFileSize(item.Size);
    }
    else
    {
        notification.SetFileSize(item.Path);
    }
}

}  // namespace

HRESULT ArchiveAgent::OnCompleteTerminationHandler::operator()()
{
    switch (m_object)
    {
        case ArchiveAgent::OnComplete::File:
            if (!DeleteFile(m_FullPath.c_str()))
            {
                if (GetLastError() != ERROR_FILE_NOT_FOUND)
                    return HRESULT_FROM_WIN32(GetLastError());
            }
            break;
        case ArchiveAgent::OnComplete::Directory:
            if (!RemoveDirectory(m_FullPath.c_str()))
            {
                if (GetLastError() != ERROR_FILE_NOT_FOUND)
                    return HRESULT_FROM_WIN32(GetLastError());
            }
            break;
        default:
            break;
    }

    return S_OK;
}

ArchiveAgent::OnComplete::OnComplete(
    Action action,
    Object object,
    const std::wstring& name,
    const std::wstring& fullpath)
    : m_action(action)
    , m_Name(name)
    , m_object(object)
    , m_FullPath(fullpath)
    , m_pTerminationHandler(nullptr)
    , m_hr(E_PENDING)
{
    if (action == OnComplete::Delete)
    {
        m_pTerminationHandler = std::make_shared<OnCompleteTerminationHandler>(
            L"Directory delete upon abnormal termination", m_object, fullpath);
        Robustness::AddTerminationHandler(m_pTerminationHandler);
    }
}

ArchiveAgent::OnComplete::~OnComplete()
{
    CancelTerminationHandler();
}

void ArchiveAgent::OnComplete::CancelTerminationHandler()
{
    if (m_pTerminationHandler != nullptr)
    {
        std::shared_ptr<OnCompleteTerminationHandler> pTerminationHandler = m_pTerminationHandler;
        m_pTerminationHandler = nullptr;
        Robustness::RemoveTerminationHandler(pTerminationHandler);
        pTerminationHandler = nullptr;
    }
}

HRESULT ArchiveAgent::CompleteOnFlush(bool bFinal)
{
    std::for_each(begin(m_PendingCompletions), end(m_PendingCompletions), [this, bFinal](OnComplete& action) {
        HRESULT hr = E_FAIL;

        switch (action.GetObjectType())
        {
            case OnComplete::Directory: {
                switch (action.GetAction())
                {
                    case OnComplete::Delete: {
                        if (!action.Fullpath().empty())
                        {
                            if (!RemoveDirectory(action.Fullpath().c_str()))
                            {
                                hr = action.m_hr = HRESULT_FROM_WIN32(GetLastError());
                                Log::Error(L"Failed to delete directory '{}' [{}]", action.Fullpath(), SystemError(hr));
                            }
                            else
                            {
                                Log::Debug(L"Successfully deleted file {}", action.Fullpath());
                                action.m_hr = S_OK;
                            }
                        }
                    }
                    break;
                    default:
                        break;
                }
            }
            break;
            case OnComplete::File: {
                switch (action.GetAction())
                {
                    case OnComplete::Delete: {
                        if (!action.Fullpath().empty())
                        {
                            if (!DeleteFile(action.Fullpath().c_str()))
                            {
                                hr = action.m_hr = HRESULT_FROM_WIN32(GetLastError());
                                Log::Error(L"Failed to delete directory '{}' [{}]", action.Fullpath(), SystemError(hr));
                            }
                            else
                            {
                                Log::Debug(L"Successfully deleted file '{}'", action.Fullpath());
                                action.m_hr = S_OK;
                            }
                        }
                    }
                    break;
                    default:
                        break;
                }
            }
            break;
            default:
                break;
        }
        if (SUCCEEDED(hr))
            action.CancelTerminationHandler();
    });

    auto new_end = std::remove_if(begin(m_PendingCompletions), end(m_PendingCompletions), [](const OnComplete& action) {
        return SUCCEEDED(action.m_hr);
    });
    m_PendingCompletions.erase(new_end, end(m_PendingCompletions));
    return S_OK;
}

void ArchiveAgent::run()
{
    HRESULT hr = E_FAIL;

    ArchiveMessage::Message request = GetRequest();

    while (request)
    {
        switch (request->GetRequest())
        {
            case ArchiveMessage::OpenArchive: {
                ArchiveNotification::Notification notification;

                if (m_compressor != nullptr)
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request,
                        ArchiveNotification::ArchiveStarted,
                        E_FAIL,
                        request->Name(),
                        L"Already creating archive");
                }
                else if (request->GetStream() == nullptr)
                {
                    ArchiveFormat fmt = OrcArchive::GetArchiveFormat(request->Name());
                    if (fmt == ArchiveFormat::Unknown)
                        notification = ArchiveNotification::MakeFailureNotification(
                            request,
                            ArchiveNotification::ArchiveStarted,
                            E_FAIL,
                            request->Name(),
                            L"Unsupported format extension");
                    else
                    {
                        m_compressor = ArchiveCreate::MakeCreate(fmt, request->GetComputeHash());

                        if (!request->GetCompressionLevel().empty())
                            m_compressor->SetCompressionLevel(request->GetCompressionLevel());
                        m_compressor->SetPassword(request->GetPassword());

                        if (m_compressor == nullptr)
                            notification = ArchiveNotification::MakeFailureNotification(
                                request,
                                ArchiveNotification::ArchiveStarted,
                                E_FAIL,
                                request->Name(),
                                L"Failed to create compressor");
                        else
                        {
                            if (!request->GetCompressionLevel().empty())
                                m_compressor->SetCompressionLevel(request->GetCompressionLevel());

                            if (FAILED(hr = m_compressor->InitArchive(request->Name().c_str())))
                                notification = ArchiveNotification::MakeFailureNotification(
                                    request,
                                    ArchiveNotification::ArchiveStarted,
                                    E_FAIL,
                                    request->Name(),
                                    L"Failed to create compressor");
                            else
                            {
                                m_cabName = request->Name();

                                m_compressor->SetCallback([this, request](const OrcArchive::ArchiveItem& item) {
                                    auto notification = ArchiveNotification::MakeSuccessNotification(
                                        request, ArchiveNotification::FileAddition, item.NameInArchive);
                                    if (notification)
                                    {
                                        SetFileSize(item, *notification);
                                        SendResult(notification);
                                    }
                                });

                                notification = ArchiveNotification::MakeArchiveStartedSuccessNotification(
                                    request, request->Name(), request->Name(), request->GetCompressionLevel());
                            }
                        }
                    }
                }
                else if (request->GetStream() != nullptr && request->GetArchiveFormat() != ArchiveFormat::Unknown)
                {
                    m_compressor = ArchiveCreate::MakeCreate(request->GetArchiveFormat(), request->GetComputeHash());

                    if (m_compressor == nullptr)
                        notification = ArchiveNotification::MakeFailureNotification(
                            request,
                            ArchiveNotification::ArchiveStarted,
                            E_FAIL,
                            request->Name(),
                            L"Failed to create compressor");
                    else
                    {
                        if (!request->GetCompressionLevel().empty())
                            m_compressor->SetCompressionLevel(request->GetCompressionLevel());

                        if (FAILED(hr = m_compressor->InitArchive(request->GetStream())))
                            notification = ArchiveNotification::MakeFailureNotification(
                                request,
                                ArchiveNotification::ArchiveStarted,
                                E_FAIL,
                                request->Name(),
                                L"Failed to create compressor");
                        else
                        {
                            m_cabName = request->Name();

                            m_compressor->SetCallback([this, request](const OrcArchive::ArchiveItem& item) {
                                auto notification = ArchiveNotification::MakeSuccessNotification(
                                    request, ArchiveNotification::FileAddition, item.NameInArchive);
                                if (notification)
                                {
                                    SetFileSize(item, *notification);
                                    SendResult(notification);
                                }
                            });

                            notification = ArchiveNotification::MakeArchiveStartedSuccessNotification(
                                request, request->Name(), request->Name(), request->GetCompressionLevel());
                        }
                    }
                }
                else
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request, ArchiveNotification::ArchiveStarted, E_FAIL, request->Name(), L"Invalid request");
                }
                if (notification)
                    SendResult(notification);
            }
            break;
            case ArchiveMessage::AddFile: {
                ArchiveNotification::Notification notification;

                hr = m_compressor->AddFile(
                    request->NameInArchive().c_str(), request->SourcePath().c_str(), request->GetDeleteWhenDone());
                if (FAILED(hr))
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request, ArchiveNotification::FileAddition, hr, request->Name(), L"AddFileToCab failed");
                }

                if (notification)
                    SendResult(notification);
            }
            break;
            case ArchiveMessage::AddDirectory: {
                WIN32_FIND_DATA ffd;

                std::wstring strSearchString(request->SourcePath().c_str());

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
                    auto notification = ArchiveNotification::MakeFailureNotification(
                        request,
                        ArchiveNotification::DirectoryAddition,
                        HRESULT_FROM_WIN32(GetLastError()),
                        request->Name(),
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
                            std::wstring strFileName = request->SourcePath();
                            strFileName.append(L"\\");
                            strFileName.append(ffd.cFileName);

                            std::wstring strCabbedName;
                            strCabbedName.assign(request->NameInArchive());
                            strCabbedName.append(L"\\");
                            strCabbedName.append(ffd.cFileName);
                            ArchiveNotification::Notification notification;

                            hr = m_compressor->AddFile(
                                strCabbedName.c_str(), strFileName.c_str(), request->GetDeleteWhenDone());
                            if (FAILED(hr))
                            {
                                notification = ArchiveNotification::MakeFailureNotification(
                                    request,
                                    ArchiveNotification::FileAddition,
                                    hr,
                                    strFileName,
                                    L"AddFileToCab failed");
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
                        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
                        Log::Warn("Failed to close FindFile while adding directory to CAB [{}]", SystemError(hr));
                    }
                }

                m_PendingCompletions.emplace_back(
                    OnComplete::Action::Delete, OnComplete::Object::Directory, request->Name(), request->Name());
            }
            break;
            case ArchiveMessage::AddStream: {
                ArchiveNotification::Notification notification;

                hr = m_compressor->AddStream(
                    request->NameInArchive().c_str(), request->SourcePath().c_str(), request->GetStream());
                if (FAILED(hr))
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request, ArchiveNotification::StreamAddition, hr, request->Name(), L"AddStream failed");
                }

                if (notification)
                {
                    SendResult(notification);
                }
            }
            break;
            case ArchiveMessage::FlushQueue: {
                ArchiveNotification::Notification notification;

                hr = m_compressor->FlushQueue();
                if (FAILED(hr))
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request, ArchiveNotification::FlushQueue, hr, request->Name(), L"FlushQueue failed");
                    SendResult(notification);
                }

                hr = CompleteOnFlush(false);
                if (FAILED(hr))
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request,
                        ArchiveNotification::FlushQueue,
                        hr,
                        request->Name(),
                        L"FlushQueue completion actions failed");
                    SendResult(notification);
                }
            }
            break;
            case ArchiveMessage::Complete: {
                ArchiveNotification::Notification notification;

                hr = m_compressor->Complete();
                if (FAILED(hr))
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request, ArchiveNotification::ArchiveComplete, hr, m_cabName, L"Complete failed");
                }
                else
                {
                    notification = ArchiveNotification::MakeSuccessNotification(
                        request, ArchiveNotification::ArchiveComplete, m_cabName);
                    if (notification)
                    {
                        notification->SetFileSize(m_cabName);
                    }
                }

                if (notification)
                {
                    SendResult(notification);
                }

                hr = CompleteOnFlush(true);
                if (FAILED(hr))
                {
                    notification = ArchiveNotification::MakeFailureNotification(
                        request,
                        ArchiveNotification::FlushQueue,
                        hr,
                        request->Name(),
                        L"FlushQueue completion actions failed");

                    SendResult(notification);
                }
            }
            break;
        }

        ArchiveMessage::Request type = request->GetRequest();

        request = nullptr;

        if (type == ArchiveMessage::Complete)
        {
            done();
        }
        else
        {
            request = GetRequest();
        }
    }
    return;
}
