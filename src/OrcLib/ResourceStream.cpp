//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ResourceStream.h"

#include "EmbeddedResource.h"
#include "Temporary.h"

#include <string>
#include <string_view>

using namespace Orc;

ResourceStream::ResourceStream()
    : MemoryStream()
{
}

ResourceStream::~ResourceStream()
{
    Close();
}

HRESULT ResourceStream::OpenForReadWrite()
{
    Log::Error("Invalid write to read-only memory stream");
    return E_ACCESSDENIED;
}

HRESULT ResourceStream::OpenForReadOnly(__in const HINSTANCE hInstance, __in WORD resourceID)
{
    HGLOBAL hFileResource = NULL;
    HRSRC hResource = NULL;

    // First find and load the required resource
    if ((hResource = FindResource(hInstance, MAKEINTRESOURCE(resourceID), L"BINARY")) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed FindResource '{}' (code: {:#x})", resourceID, hr);
        return hr;
    }

    if ((hFileResource = LoadResource(hInstance, hResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LoadResource '{}' (code: {:#x})", resourceID, hr);
        return hr;
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;

    if ((lpFile = LockResource(hFileResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LockResource '{}' (code: {:#x})", resourceID, hr);
        return hr;
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hInstance, hResource)) == 0)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed SizeofResource '{}' (code: {:#x})", resourceID, hr);
        return hr;
    }
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFileResource, hFileResource);
        std::swap(m_hResource, hResource);
    }

    return MemoryStream::OpenForReadOnly(lpFile, dwSize);
}

HRESULT ResourceStream::OpenForReadOnly(__in const HINSTANCE hInstance, __in HRSRC hRes)
{
    HGLOBAL hFileResource = NULL;
    HRSRC hResource = NULL;

    // First find and load the required resource
    hResource = hRes;

    if ((hFileResource = LoadResource(hInstance, hResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LoadResource (code: {:#x})", hr);
        return hr;
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;
    if ((lpFile = LockResource(hFileResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LockResource (code: {:#x})", hr);
        return hr;
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hInstance, hResource)) == 0)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed SizeofResource (code: {:#x})", hr);
        return hr;
    }
    {
        ScopedLock sl(m_cs);
        std::swap(m_hFileResource, hFileResource);
        std::swap(m_hResource, hResource);
    }

    return MemoryStream::OpenForReadOnly(lpFile, dwSize);
}

HRESULT Orc::ResourceStream::OpenForReadOnly(const std::wstring& strResourceSpec)
{
    using namespace std::string_view_literals;

    if (!EmbeddedResource::IsResourceBased(strResourceSpec.c_str()))
    {
        Log::Error(L"Invalid ressource reference '{}'", strResourceSpec);
        return E_FAIL;
    }

    std::wstring ResName, MotherShip, NameInArchive, Format;
    if (auto hr = EmbeddedResource::SplitResourceReference(strResourceSpec, MotherShip, ResName, NameInArchive, Format);
        FAILED(hr))
        return hr;

    if (Format != L"res"sv)
    {
        Log::Error(L"Invalid ressource format '{}'. Only the res:# format is currently supported", Format);
        return E_FAIL;
    }

    HMODULE hMod = NULL;
    HRSRC hRes = NULL;
    std::wstring strBinaryPath;

    if (auto hr = EmbeddedResource::LocateResource(
            MotherShip, ResName, EmbeddedResource::BINARY(), hMod, hRes, strBinaryPath);
        FAILED(hr))
        return hr;

    if (auto hr = OpenForReadOnly(hMod, hRes); FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT ResourceStream::Close()
{
    HGLOBAL hFileResource = NULL;
    {
        ScopedLock sl(m_cs);
        std::swap(hFileResource, m_hFileResource);
    }

    if (hFileResource != NULL)
    {
        FreeResource(hFileResource);
    }
    return MemoryStream::Close();
}
