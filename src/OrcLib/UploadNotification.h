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

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API UploadNotification
{

public:
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

	static Notification MakeSuccessNotification(Type type, const std::wstring& keyword);
    static Notification
		MakeFailureNotification(Type type, HRESULT hr, const std::wstring& keyword, const std::wstring& description);

    static Notification
		MakeUploadStartedSuccessNotification(const std::wstring& keyword, const std::wstring& strFileName);

	static Notification MakeAddFileSucessNotification(const std::wstring& keyword, const std::wstring& strFileName);

    Status GetStatus() const { return m_Status; };
    HRESULT GetHResult() const { return m_hr; };
    Type GetType() const { return m_Type; };

    const std::wstring& Keyword() const { return m_keyword; };
    const std::wstring& Description() const { return m_description; }
    const std::wstring& GetFileName() const { return m_path; }

    ~UploadNotification(void);

private:
    Status m_Status;
    Type m_Type;
    std::wstring m_keyword;
    std::wstring m_description;
    std::wstring m_path;
    HRESULT m_hr;

protected:

    UploadNotification(Type type, Status status, const std::wstring& keyword, const std::wstring& descr, HRESULT hr)
        : m_Type(type)
        , m_Status(status)
        , m_keyword(keyword)
        , m_description(descr)
        , m_hr(hr)
    {
    }

};

}  // namespace Orc

#pragma managed(pop)
