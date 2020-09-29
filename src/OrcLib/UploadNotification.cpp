//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl (ANSSI)
//
#include "stdafx.h"

#include "UploadNotification.h"

using namespace Orc;

namespace {

// Enable the use of std::make_shared with UploadNotification protected constructor
struct UploadNotificationT : public Orc::UploadNotification
{
    template <typename... Args>
    inline UploadNotificationT(Args&&... args)
        : UploadNotification(std::forward<Args>(args)...)
    {
    }
};

}  // namespace

Orc::UploadNotification::Notification Orc::UploadNotification::MakeSuccessNotification(
    const UploadMessage::Ptr& request,
    Type type,
    const std::wstring& source,
    const std::wstring& destination)
{
    return std::make_shared<UploadNotificationT>(type, Success, request, source, destination, S_OK, L"");
}

Orc::UploadNotification::Notification Orc::UploadNotification::MakeFailureNotification(
    const UploadMessage::Ptr& request,
    Type type,
    const std::wstring& source,
    const std::wstring& destination,
    HRESULT hr,
    const std::wstring& description)
{
    return std::make_shared<UploadNotificationT>(type, Failure, request, source, destination, hr, description);
}

UploadNotification::~UploadNotification() {}
