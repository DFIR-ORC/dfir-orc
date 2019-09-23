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
#include "OutputSpec.h"

#include <memory>
#include <agents.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ArchiveMessage : public std::enable_shared_from_this<ArchiveMessage>
{
    friend class std::shared_ptr<ArchiveMessage>;
    friend class std::_Ref_count_obj<ArchiveMessage>;

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

    using Message = std::shared_ptr<ArchiveMessage>;

    using ITarget = Concurrency::ITarget<Message>;
    using ISource = Concurrency::ISource<Message>;

private:
    Request m_Request;
    Status m_Status;

    std::wstring m_Name;
    std::wstring m_Keyword;
    std::wstring m_Pattern;
    std::wstring m_CompressionLevel;

    ArchiveFormat m_Format;
    std::shared_ptr<ByteStream> m_Stream;

    bool m_bDeleteWhenDone;
    bool m_bHashData;
    DWORD m_dwXORPattern;

    ArchiveMessage(Request request)
        : m_Request(request)
        , m_Status(Open)
        , m_bHashData(false)
        , m_dwXORPattern(0L)
        , m_bDeleteWhenDone(false)
        , m_Format(ArchiveFormat::Unknown) {};

public:
    static Message MakeOpenRequest(const std::wstring& szArchiveName, const std::wstring& strCompressionLevel = L"");
    static Message MakeOpenRequest(
        const std::wstring& szArchiveName,
        const ArchiveFormat aFormat,
        const std::shared_ptr<ByteStream>& aStream,
        const std::wstring& strCompressionLevel = L"");
    static Message MakeOpenRequest(const OutputSpec& anOutput);

    static Message MakeAddFileRequest(
        const std::wstring& szCabbedName,
        const std::wstring& szFileName,
        bool bHashData = false,
        DWORD dwXORPattern = 0,
        bool bDeleteWhenDone = false);
    static Message MakeAddDirectoryRequest(
        const std::wstring& szDirCabbedName,
        const std::wstring& szDirName,
        const std::wstring& szPattern,
        bool bHashData = false,
        DWORD dwXORPattern = 0,
        bool bDeleteWhenDone = false);
    static Message MakeAddStreamRequest(
        const std::wstring& szCabbedName,
        const std::shared_ptr<ByteStream>& aStream,
        bool bHashData = false,
        DWORD dwXORPattern = 0);

    static Message MakeFlushQueueRequest();
    static Message MakeCompleteRequest();

    static Message MakeCancellationRequest();

    Request GetRequest() const { return m_Request; };
    Status GetStatus() const { return m_Status; };
    DWORD GetXORPattern() const { return m_dwXORPattern; };
    bool GetComputeHash() const { return m_bHashData; };
    bool GetDeleteWhenDone() const { return m_bDeleteWhenDone; };

    const std::wstring& Name() const { return m_Name; };
    const std::wstring& Keyword() const { return m_Keyword; };
    const std::wstring& Pattern() const { return m_Pattern; };

    const std::shared_ptr<ByteStream>& GetStream() const { return m_Stream; };

    ArchiveFormat GetArchiveFormat() const { return m_Format; };
    const std::wstring& GetCompressionLevel() const { return m_CompressionLevel; };

    ~ArchiveMessage(void);
};
}  // namespace Orc

#pragma managed(pop)
