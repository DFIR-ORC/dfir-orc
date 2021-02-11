//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ByteStream.h"
#include "Archive.h"
#include "OutputSpec.h"

#include <memory>
#include <agents.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ArchiveMessage : public std::enable_shared_from_this<ArchiveMessage>
{
public:
    enum Request
    {
        OpenArchive,
        AddFile,
        AddDirectory,
        AddStream,
        FlushQueue,
        Cancel,
        Complete
    };

    enum Status
    {
        Open,
        Wait,
        Ready,
        InProgress,
        Done
    };

    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<std::shared_ptr<ArchiveMessage>>;

    using Ptr = std::shared_ptr<const ArchiveMessage>;
    using Message = std::shared_ptr<ArchiveMessage>;

    using ITarget = Concurrency::ITarget<Message>;
    using ISource = Concurrency::ISource<Message>;

private:
    Request m_request;
    Status m_status;

    std::wstring m_archiveFileName;
    std::wstring m_sourcePath;
    std::wstring m_command;
    std::wstring m_nameInArchive;
    std::wstring m_pattern;
    std::wstring m_compressionLevel;
    std::wstring m_password;

    ArchiveFormat m_format;
    std::shared_ptr<ByteStream> m_stream;

    bool m_bDeleteWhenDone;
    bool m_bHashData;

protected:
    ArchiveMessage(Request request)
        : m_request(request)
        , m_status(Open)
        , m_bHashData(false)
        , m_bDeleteWhenDone(false)
        , m_format(ArchiveFormat::Unknown) {};

public:
    static Message MakeOpenRequest(const std::wstring& archiveFileName, const std::wstring& compressionLevel = L"");

    static Message MakeOpenRequest(
        const std::wstring& archiveFileName,
        const ArchiveFormat format,
        const std::shared_ptr<ByteStream>& stream,
        const std::wstring& compressionLevel = L"",
        const std::wstring& password = L"");

    static Message MakeOpenRequest(const OutputSpec& output);

    static Message MakeAddFileRequest(
        const std::wstring& nameInArchive,
        const std::wstring& fileName,
        bool bHashData = false,
        bool bDeleteWhenDone = false);

    static Message MakeAddDirectoryRequest(
        const std::wstring& nameInArchive,
        const std::wstring& dirName,
        const std::wstring& szPattern,
        bool bHashData = false,
        bool bDeleteWhenDone = false);

    static Message MakeAddStreamRequest(
        const std::wstring& szCabbedName,
        const std::shared_ptr<ByteStream>& aStream,
        bool bHashData = false);

    static Message MakeFlushQueueRequest();
    static Message MakeCompleteRequest();

    static Message MakeCancellationRequest();

    Request GetRequest() const { return m_request; }
    Status GetStatus() const { return m_status; }
    bool GetComputeHash() const { return m_bHashData; }
    bool GetDeleteWhenDone() const { return m_bDeleteWhenDone; }

    const std::wstring& Name() const { return m_archiveFileName; }

    const std::wstring& SourcePath() const { return m_sourcePath; }

    const std::wstring& NameInArchive() const { return m_nameInArchive; }

    const std::wstring& Command() const { return m_command; }
    void SetCommand(const std::wstring& command) { m_command = command; }

    const std::wstring& Pattern() const { return m_pattern; }

    const std::shared_ptr<ByteStream>& GetStream() const { return m_stream; }

    ArchiveFormat GetArchiveFormat() const { return m_format; };
    const std::wstring& GetCompressionLevel() const { return m_compressionLevel; }
    const std::wstring& GetPassword() const { return m_password; }

    virtual ~ArchiveMessage();
};
}  // namespace Orc

#pragma managed(pop)
