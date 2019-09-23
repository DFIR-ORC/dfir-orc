//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "BoundedBuffer.h"
#include "ByteStream.h"

#include "Archive.h"

#include <memory>

#include <agents.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API UploadMessage : public std::enable_shared_from_this<UploadMessage>
{
    friend class std::shared_ptr<UploadMessage>;
    friend class std::_Ref_count_obj<UploadMessage>;

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

    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<std::shared_ptr<UploadMessage>>;

    using Message = std::shared_ptr<UploadMessage>;

    using ITarget = Concurrency::ITarget<Message>;
    using ISource = Concurrency::ISource<Message>;

private:
    Request m_Request;
    Status m_Status;

    std::wstring m_RemoteServerName;
    std::wstring m_RemoteRoot;
    std::wstring m_JobName;

    std::wstring m_Local;
    std::wstring m_Remote;
    std::wstring m_Pattern;

    std::shared_ptr<ByteStream> m_Stream;

    bool m_bDeleteWhenDone;

    UploadMessage(Request request)
        : m_Request(request)
        , m_Status(Opened)
        , m_bDeleteWhenDone(false) {};

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

    Request GetRequest() const { return m_Request; };
    Status GetStatus() const { return m_Status; };
    bool GetDeleteWhenDone() const { return m_bDeleteWhenDone; };

    const std::wstring& JobName() const { return m_JobName; };
    const std::wstring& RemoteServerName() const { return m_RemoteServerName; };
    const std::wstring& RemoteRootPath() const { return m_RemoteRoot; };

    const std::wstring& LocalName() const { return m_Local; };
    const std::wstring& RemoteName() const { return m_Remote; };
    const std::wstring& Pattern() const { return m_Pattern; };

    const std::shared_ptr<ByteStream>& GetStream() const { return m_Stream; };

    ~UploadMessage(void);
};
}  // namespace Orc

#pragma managed(pop)
