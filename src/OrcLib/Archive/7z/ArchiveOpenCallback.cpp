//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include <7zip/7zip.h>

#include "ArchiveOpenCallback.h"

using namespace Orc::Archive;

STDMETHODIMP ArchiveOpenCallback::SetTotal(const UInt64*, const UInt64*)
{
    return S_OK;
}

STDMETHODIMP ArchiveOpenCallback::SetCompleted(const UInt64*, const UInt64*)
{
    return S_OK;
}

STDMETHODIMP ArchiveOpenCallback::CryptoGetTextPassword(BSTR*)
{
    return E_NOTIMPL;
}
