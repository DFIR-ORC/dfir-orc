//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "SparseStream.h"

HRESULT Orc::SparseStream::OpenFile(
	__in PCWSTR pwzPath,
	__in DWORD dwDesiredAccess,
	__in DWORD dwSharedMode,
	__in_opt PSECURITY_ATTRIBUTES pSecurityAttributes,
	__in DWORD dwCreationDisposition,
	__in DWORD dwFlagsAndAttributes,
	__in_opt HANDLE hTemplate)
{

	if (auto hr = Orc::FileStream::OpenFile(pwzPath, dwDesiredAccess, dwSharedMode, pSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplate); FAILED(hr))
		return hr;
	
	if (!DeviceIoControl(m_hFile, FSCTL_SET_SPARSE, NULL, 0L, NULL, 0L, NULL, NULL))
	{
		auto hr = HRESULT_FROM_WIN32(GetLastError());
		log::Error(_L_, hr, L"Failed to set file \"%s\" sparse\r\n", pwzPath);
		return hr;
	}

	return S_OK;
}

STDMETHODIMP_(HRESULT __stdcall) Orc::SparseStream::SetSize(ULONG64 ullSize)
{
	return Orc::FileStream::SetSize(ullSize);
}


Orc::SparseStream::~SparseStream()
{
}

