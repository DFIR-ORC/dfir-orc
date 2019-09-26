//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "UploadNotification.h"

using namespace Orc;

struct UploadNotification_make_shared_enabler : public Orc::UploadNotification {
	inline UploadNotification_make_shared_enabler(Orc::UploadNotification::Type type, Orc::UploadNotification::Status status, const std::wstring& keyword, const std::wstring& descr, HRESULT hr) :
		Orc::UploadNotification(type, status, keyword, descr, hr) {}
};

Orc::UploadNotification::Notification Orc::UploadNotification::MakeSuccessNotification(Type type, const std::wstring& keyword)
{
	return std::make_shared<UploadNotification_make_shared_enabler>(type, Success, keyword, L"", S_OK);
}

Orc::UploadNotification::Notification Orc::UploadNotification::MakeFailureNotification(Type type, HRESULT hr, const std::wstring& keyword, const std::wstring& description)
{
	return std::make_shared<UploadNotification_make_shared_enabler>(type, Failure, keyword, description, hr);
}

Orc::UploadNotification::Notification Orc::UploadNotification::MakeUploadStartedSuccessNotification(const std::wstring& keyword, const std::wstring& strFileName)
{
	auto retval =
		std::make_shared<UploadNotification_make_shared_enabler>(Started, Success, keyword, L"Archive creation started", S_OK);
	retval->m_path = strFileName;
	return retval;
}

Orc::UploadNotification::Notification Orc::UploadNotification::MakeAddFileSucessNotification(const std::wstring& keyword, const std::wstring& strFileName)
{
	auto retval = std::make_shared<UploadNotification_make_shared_enabler>(FileAddition, Success, keyword, L"", S_OK);
	retval->m_path = strFileName;
	return retval;
}

UploadNotification::~UploadNotification(void) {}
