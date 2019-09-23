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

#include "LogFileWriter.h"

#include <string>
#include <string_view>

using namespace Orc;

ResourceStream::ResourceStream(logger pLog)
    : MemoryStream(std::move(pLog))
{
}

ResourceStream::~ResourceStream()
{
    Close();
}

HRESULT ResourceStream::OpenForReadWrite()
{
    log::Error(_L_, E_ACCESSDENIED, L"Invalid write to read-only memory stream\r\n");
    return E_ACCESSDENIED;
}

HRESULT ResourceStream::OpenForReadOnly(__in const HINSTANCE hInstance, __in WORD resourceID)
{
    HGLOBAL hFileResource = NULL;
    HRSRC hResource = NULL;

    // First find and load the required resource
    if ((hResource = FindResource(hInstance, MAKEINTRESOURCE(resourceID), L"BINARY")) == NULL)
    {
        log::Verbose(_L_, L"WARNING: Invalid resource id (%d) could not be found\r\n", resourceID);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if ((hFileResource = LoadResource(hInstance, hResource)) == NULL)
    {
        log::Verbose(_L_, L"WARNING: Could not load ressource\r\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;

    if ((lpFile = LockResource(hFileResource)) == NULL)
    {
        log::Verbose(_L_, L"Could not lock ressource\r\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hInstance, hResource)) == 0)
    {
        log::Verbose(_L_, L"Could not compute ressource size\r\n");
        return HRESULT_FROM_WIN32(GetLastError());
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
        log::Verbose(_L_, L"WARNING: Could not load ressource\r\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;
    if ((lpFile = LockResource(hFileResource)) == NULL)
    {
        log::Verbose(_L_, L"WARNING: Could not lock ressource\r\n");
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hInstance, hResource)) == 0)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"Could not compute ressource size\r\n");
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
        log::Error(_L_, E_FAIL, L"Invalid ressource reference \"%s\"\r\n", strResourceSpec.c_str());
        return E_FAIL;
    }

    std::wstring ResName, MotherShip, NameInArchive, Format;
    if (auto hr =
            EmbeddedResource::SplitResourceReference(_L_, strResourceSpec, MotherShip, ResName, NameInArchive, Format);
        FAILED(hr))
        return hr;

    if (Format != L"res"sv)
    {
        log::Error(
            _L_,
            E_FAIL,
            L"Invalid ressource format \"%s\". Only the res:# format is currently supported\r\n",
            Format.c_str());
        return E_FAIL;
    }

    HMODULE hMod = NULL;
    HRSRC hRes = NULL;
    std::wstring strBinaryPath;

    if (auto hr = EmbeddedResource::LocateResource(
            _L_, MotherShip, ResName, EmbeddedResource::BINARY(), hMod, hRes, strBinaryPath);
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
