//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <string>
#include <memory>

#include <agents.h>

#include "BinaryBuffer.h"
#include "ArchiveMessage.h"

#pragma managed(push, off)

namespace Orc {

class ArchiveNotification
{
public:
    enum Status
    {
        Success,
        Failure
    };

    enum Type
    {
        Started,
        ArchiveStarted,
        FileAddition,
        DirectoryAddition,
        StreamAddition,
        FlushQueue,
        Cancelled,
        ArchiveComplete
    };

    using Notification = std::shared_ptr<ArchiveNotification>;
    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<Notification>;

    using ITarget = Concurrency::ITarget<Notification>;
    using ISource = Concurrency::ISource<Notification>;

private:
    ArchiveMessage::Ptr m_request;
    Status m_status;
    Type m_type;
    std::wstring m_keyword;
    std::wstring m_fileName;
    std::wstring m_description;
    std::wstring m_compressionLevel;
    HRESULT m_hr;

    CBinaryBuffer m_md5;
    CBinaryBuffer m_sha1;

protected:
    ArchiveNotification(
        const ArchiveMessage::Ptr& request,
        Type type,
        Status status,
        const std::wstring& keyword,
        const std::wstring& descr,
        HRESULT hr)
        : m_request(request)
        , m_type(type)
        , m_status(status)
        , m_keyword(keyword)
        , m_description(descr)
        , m_hr(hr)
    {
    }

public:
    static Notification
    MakeSuccessNotification(const ArchiveMessage::Ptr& request, Type type, const std::wstring& keyword);
    static Notification MakeFailureNotification(
        const ArchiveMessage::Ptr& request,
        Type type,
        HRESULT hr,
        const std::wstring& keyword,
        const std::wstring& description);

    static Notification MakeArchiveStartedSuccessNotification(
        const ArchiveMessage::Ptr& request,
        const std::wstring& keyword,
        const std::wstring& strFileName,
        const std::wstring& strCompressionLevel);

    Status GetStatus() const { return m_status; };
    HRESULT GetHResult() const { return m_hr; };
    Type GetType() const { return m_type; };

    const std::wstring& Keyword() const { return m_keyword; };
    const std::wstring& CommandSet() const { return m_request->CommandSet(); }
    const std::wstring& Description() const { return m_description; }
    const std::wstring& GetFileName() const { return m_fileName; }
    const std::wstring& GetCompressionLevel() const { return m_compressionLevel; }

    const CBinaryBuffer& MD5() const { return m_md5; };
    const CBinaryBuffer& SHA1() const { return m_sha1; };

    virtual ~ArchiveNotification();
};

}  // namespace Orc

#pragma managed(pop)
