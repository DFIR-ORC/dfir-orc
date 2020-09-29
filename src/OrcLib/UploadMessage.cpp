//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "UploadMessage.h"

using namespace Orc;

namespace {

// Enable the use of std::make_shared with UploadMessage protected constructor
struct UploadMessageT : public Orc::UploadMessage
{
    template <typename... Args>
    inline UploadMessageT(Args&&... args)
        : UploadMessage(std::forward<Args>(args)...)
    {
    }
};

}  // namespace

UploadMessage::Message UploadMessage::MakeUploadFileRequest(
    const std::wstring& remotePath,
    const std::wstring& localPath,
    bool bDeleteWhenDone)
{
    auto message = std::make_shared<::UploadMessageT>(UploadMessage::UploadFile);
    message->m_status = UploadMessage::Ready;
    message->m_remotePath = remotePath;
    message->m_localPath = localPath;
    message->m_bDeleteWhenDone = bDeleteWhenDone;
    message->m_jobName = fmt::format(L"Upload file '{}' to '{}'", localPath, remotePath);
    return message;
}

UploadMessage::Message UploadMessage::MakeUploadDirectoryRequest(
    const std::wstring& strRemoteName,
    const std::wstring& strDirName,
    const std::wstring& strPattern,
    bool bDeleteOnCompletion)
{
    auto message = std::make_shared<::UploadMessageT>(UploadMessage::UploadDirectory);
    message->m_status = UploadMessage::Ready;
    message->m_remotePath = strRemoteName;
    message->m_localPath = strDirName;
    message->m_pattern = strPattern;
    message->m_bDeleteWhenDone = bDeleteOnCompletion;
    message->m_jobName =
        fmt::format(L"Upload directory '{}' to '{}' (pattern: {})", strDirName, strRemoteName, strPattern);

    return message;
}

UploadMessage::Message
UploadMessage::MakeUploadStreamRequest(const std::wstring& strRemoteName, const std::shared_ptr<ByteStream>& aStream)
{
    auto message = std::make_shared<::UploadMessageT>(UploadMessage::UploadStream);
    message->m_status = UploadMessage::Ready;
    message->m_localPath = strRemoteName;
    message->m_remotePath = strRemoteName;
    message->m_stream = aStream;
    message->m_jobName = fmt::format(L"Upload stream to '{}'", strRemoteName);
    return message;
}

UploadMessage::Message UploadMessage::MakeRefreshJobStatusRequest()
{
    auto message = std::make_shared<UploadMessageT>(UploadMessage::RefreshJobStatus);
    message->m_jobName = L"Refresh status";
    return message;
}

UploadMessage::Message UploadMessage::MakeCompleteRequest()
{
    auto message = std::make_shared<::UploadMessageT>(UploadMessage::Complete);
    message->m_jobName = L"Complete request";
    return message;
}

UploadMessage::Message UploadMessage::MakeCancellationRequest()
{
    auto message = std::make_shared<::UploadMessageT>(UploadMessage::Cancel);
    message->m_jobName = L"Cancellation request";
    return message;
}
