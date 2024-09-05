//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <functional>
#include <memory>
#include <string>

#include <agents.h>

#include "UploadMessage.h"
#include "Utils/TypeTraits.h"

#pragma managed(push, off)

namespace Orc {

class UploadNotification
{
public:
    using Callback = std::function<void(const UploadNotification& callback)>;

    typedef enum _Status
    {
        Success,
        Failure
    } Status;

    typedef enum _Type
    {
        Started,
        FileAddition,
        DirectoryAddition,
        StreamAddition,
        Cancelled,
        UploadComplete,
        JobComplete
    } Type;

    typedef std::shared_ptr<UploadNotification> Notification;
    typedef Concurrency::unbounded_buffer<Notification> UnboundedMessageBuffer;

    typedef Concurrency::ITarget<Notification> ITarget;
    typedef Concurrency::ISource<Notification> ISource;

public:
    static Notification MakeSuccessNotification(
        const UploadMessage::Ptr& request,
        Type type,
        const std::wstring& source,
        const std::wstring& destination);

    static Notification MakeFailureNotification(
        const UploadMessage::Ptr& request,
        Type type,
        const std::wstring& source,
        const std::wstring& destination,
        HRESULT hr,
        const std::wstring& description);

    Status GetStatus() const { return m_status; };
    HRESULT GetHResult() const { return m_hr; };
    Type GetType() const { return m_type; };
    const std::wstring& Keyword() const { return m_request->Keyword(); };

    const UploadMessage::Ptr& Request() const { return m_request; }
    const std::wstring& Source() const { return m_source; }
    const std::wstring& Destination() const { return m_destination; }
    const std::wstring& Description() const { return m_description; }

    void SetFileSize(const std::filesystem::path& path);
    void SetFileSize(uint64_t size) { m_fileSize = Traits::ByteQuantity(size); }
    std::optional<Traits::ByteQuantity<uint64_t>> FileSize() const { return m_fileSize; }

    virtual ~UploadNotification();

private:
    Status m_status;
    Type m_type;

    const UploadMessage::Ptr m_request;
    const std::wstring m_source;
    const std::wstring m_destination;
    std::wstring m_description;
    std::optional<Traits::ByteQuantity<uint64_t>> m_fileSize;
    HRESULT m_hr;

protected:
    UploadNotification(
        Type type,
        Status status,
        const UploadMessage::Ptr& request,
        const std::wstring& source,
        const std::wstring& destination,
        HRESULT hr,
        const std::wstring& description)
        : m_type(type)
        , m_status(status)
        , m_request(request)
        , m_source(source)
        , m_destination(destination)
        , m_hr(hr)
        , m_description(description)
    {
    }
};

}  // namespace Orc

#pragma managed(pop)
