//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ArchiveNotification.h"

namespace {

// Enable the use of std::make_shared with UploadNotification protected constructor
struct ArchiveNotificationT : public Orc::ArchiveNotification
{
    template <typename... Args>
    inline ArchiveNotificationT(Args&&... args)
        : ArchiveNotification(std::forward<Args>(args)...)
    {
    }
};

}  // namespace

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeSuccessNotification(
    const ArchiveMessage::Ptr& request,
    Type type,
    const std::wstring& keyword)
{
    return std::make_shared<ArchiveNotificationT>(request, type, Success, keyword, L"", S_OK);
}

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeFailureNotification(
    const ArchiveMessage::Ptr& request,
    Type type,
    HRESULT hr,
    const std::wstring& keyword,
    const std::wstring& description)
{
    return std::make_shared<ArchiveNotificationT>(request, type, Failure, keyword, description, hr);
}

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeArchiveStartedSuccessNotification(
    const ArchiveMessage::Ptr& request,
    const std::wstring& keyword,
    const std::wstring& strFileName,
    const std::wstring& strCompressionLevel)
{
    auto retval = std::make_shared<ArchiveNotificationT>(
        request, ArchiveStarted, Success, keyword, L"Archive creation started", S_OK);

    retval->m_fileName = strFileName;
    retval->m_compressionLevel = strCompressionLevel;
    return retval;
}

void Orc::ArchiveNotification::SetFileSize(const std::filesystem::path& path)
{
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        Log::Debug(L"Failed to get file size for '{}' [{}]", path, ec);
        return;
    }

    SetFileSize(size);
}

Orc::ArchiveNotification::~ArchiveNotification() {}
