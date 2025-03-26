//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <filesystem>

#include <7zip/7zip.h>

#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"

#include "ZipExtract.h"
#include "ZipLibrary.h"

#include "FileStream.h"
#include "CryptoHashStream.h"
#include "OutByteStreamWrapper.h"
#include "InByteStreamWrapper.h"
#include "ParameterCheck.h"

#include "ArchiveOpenCallback.h"
#include "ArchiveExtractCallback.h"

#include "PropVariant.h"

using namespace std;
using namespace lib7z;

using namespace Orc;

ZipExtract::ZipExtract(bool bComputeHash)
    : ArchiveExtract(bComputeHash)
{
}

ZipExtract::~ZipExtract(void) {}

STDMETHODIMP ZipExtract::Extract(
    __in ArchiveExtract::MakeArchiveStream makeArchiveStream,
    __in const ItemShouldBeExtractedCallback pShouldBeExtracted,
    __in ArchiveExtract::MakeOutputStream MakeWriteAbleStream)
{
    HRESULT hr = E_FAIL;

    if (makeArchiveStream == nullptr)
        return E_INVALIDARG;
    if (pShouldBeExtracted == nullptr)
        return E_INVALIDARG;
    if (MakeWriteAbleStream == nullptr)
        return E_INVALIDARG;

    const auto pZipLib = ZipLibrary::GetZipLibrary();
    if (pZipLib == nullptr)
    {
        Log::Error(L"Failed to load 7zip.dll");
        return E_FAIL;
    }

    CComPtr<IInArchive> archive;

    if (FAILED(hr = pZipLib->CreateObject(&CLSID_CFormat7z, &IID_IInArchive, reinterpret_cast<void**>(&archive))))
    {
        Log::Error(L"Failed to create archive reader [{}]", SystemError(hr));
        return hr;
    }

    std::shared_ptr<ByteStream> InputStream;

    if (FAILED(hr = makeArchiveStream(InputStream)))
    {
        Log::Error(L"Failed to make archive stream [{}]", SystemError(hr));
        return hr;
    }

    CComQIPtr<IInStream, &IID_IInStream> infile = new InByteStreamWrapper(InputStream);
    CComPtr<ArchiveOpenCallback> openCallback = new ArchiveOpenCallback();

    if ((hr = archive->Open(infile, 0, openCallback)) != S_OK)
    {
        Log::Error(L"Failed when opening archive [{}]", SystemError(hr));
        return hr;
    }

    CComPtr<ArchiveExtractCallback> extractCallback = new ArchiveExtractCallback(
        archive, pShouldBeExtracted, m_Items, MakeWriteAbleStream, m_Callback, m_bComputeHash, m_Password);

    hr = archive->Extract(NULL, (UInt32)-1, false, extractCallback);
    if (hr != S_OK)  // returning S_FALSE also indicates error
    {
        Log::Error(L"Failed when extracting archive [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}
