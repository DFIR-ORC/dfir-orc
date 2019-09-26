//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ArchiveNotification.h"

struct ArchiveNotification_make_shared_enabler : public Orc::ArchiveNotification {
	inline ArchiveNotification_make_shared_enabler(Type type, Status status, const std::wstring& keyword, const std::wstring& descr, HRESULT hr) : Orc::ArchiveNotification(type, status, keyword, descr, hr) {};
};

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeSuccessNotification(Type type, const std::wstring& keyword)
{
	return std::make_shared<ArchiveNotification_make_shared_enabler>(type, Success, keyword, L"", S_OK);
}

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeFailureNotification(Type type, HRESULT hr, const std::wstring& keyword, const std::wstring& description)
{
	return std::make_shared<ArchiveNotification_make_shared_enabler>(type, Failure, keyword, description, hr);
}

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeArchiveStartedSuccessNotification(const std::wstring& keyword, const std::wstring& strFileName, const std::wstring& strCompressionLevel)
{
	auto retval =
		std::make_shared<ArchiveNotification_make_shared_enabler>(ArchiveStarted, Success, keyword, L"Archive creation started", S_OK);

	retval->m_strFileName = strFileName;
	retval->m_strCompressionLevel = strCompressionLevel;
	return retval;
}

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeAddFileSucessNotification(const std::wstring& keyword, CBinaryBuffer& md5, CBinaryBuffer& sha1)
{
	auto retval = std::make_shared<ArchiveNotification_make_shared_enabler>(FileAddition, Success, keyword, L"", S_OK);
	retval->m_md5 = md5;
	retval->m_sha1 = sha1;
	return retval;
}

Orc::ArchiveNotification::Notification Orc::ArchiveNotification::MakeAddFileSucessNotification(const std::wstring keyword, CBinaryBuffer&& md5, CBinaryBuffer&& sha1)
{
	auto retval = std::make_shared<ArchiveNotification_make_shared_enabler>(FileAddition, Success, keyword, L"", S_OK);
	std::swap(retval->m_md5, md5);
	std::swap(retval->m_sha1, sha1);
	return retval;
}

Orc::ArchiveNotification::~ArchiveNotification(void) {}
