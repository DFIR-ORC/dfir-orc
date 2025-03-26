//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>

#include <agents.h>

#include "Archive.h"
#include "BoundedBuffer.h"
#include "ByteStream.h"

#pragma managed(push, off)

namespace Orc {

class UploadMessage : public std::enable_shared_from_this<UploadMessage>
{

public:
    enum Request
    {
        UploadFile,
        UploadDirectory,
        UploadStream,
        Cancel,
        Complete,
        RefreshJobStatus
    };

    enum Status
    {
        Opened,
        Ready,
        Suspended,
        InProgress,
        Done
    };

    using Ptr = std::shared_ptr<const UploadMessage>;
    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<std::shared_ptr<UploadMessage>>;
    using Message = std::shared_ptr<UploadMessage>;

    using ITarget = Concurrency::ITarget<Message>;
    using ISource = Concurrency::ISource<Message>;

public:
    static Message MakeUploadFileRequest(
        const std::wstring& szRemoteName,
        const std::wstring& szFileName,
        bool bDeleteWhenDone = false);
    static Message MakeUploadDirectoryRequest(
        const std::wstring& szRemoteDirName,
        const std::wstring& szDirName,
        const std::wstring& strPattern,
        bool bDeleteWhenDone = false);
    static Message
    MakeUploadStreamRequest(const std::wstring& szRemoteName, const std::shared_ptr<ByteStream>& aStream);

    static Message MakeCompleteRequest();

    static Message MakeRefreshJobStatusRequest();
    static Message MakeCancellationRequest();

    Request GetRequest() const { return m_request; };
    Status GetStatus() const { return m_status; };
    bool GetDeleteWhenDone() const { return m_bDeleteWhenDone; };

    const std::wstring& JobName() const { return m_jobName; };

    const std::wstring& Keyword() const { return m_keyword; };
    void SetKeyword(const std::wstring& keyword) { m_keyword = keyword; }

    const std::wstring& RemoteServerName() const { return m_remoteServerName; };
    const std::wstring& RemoteRootPath() const { return m_remoteRoot; };

    const std::wstring& LocalName() const { return m_localPath; };
    const std::wstring& RemoteName() const { return m_remotePath; };
    const std::wstring& Pattern() const { return m_pattern; };

    const std::shared_ptr<ByteStream>& GetStream() const { return m_stream; };

    virtual ~UploadMessage() = default;

protected:
    UploadMessage(Request request)
        : m_request(request)
        , m_status(Opened)
        , m_bDeleteWhenDone(false) {};

private:
    Request m_request;
    Status m_status;

    std::wstring m_remoteServerName;
    std::wstring m_remoteRoot;
    std::wstring m_jobName;

    std::wstring m_keyword;
    std::wstring m_localPath;
    std::wstring m_remotePath;
    std::wstring m_pattern;

    std::shared_ptr<ByteStream> m_stream;

    bool m_bDeleteWhenDone;
};

}  // namespace Orc

#pragma managed(pop)
