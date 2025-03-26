//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
        Log::Debug("Failed FindResource '{}' [{}]", resourceID, SystemError(hr));
        return hr;
    }

    if ((hFileResource = LoadResource(hInstance, hResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LoadResource '{}' [{}]", resourceID, SystemError(hr));
        return hr;
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;

    if ((lpFile = LockResource(hFileResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LockResource '{}' [{}]", resourceID, SystemError(hr));
        return hr;
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hInstance, hResource)) == 0)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed SizeofResource '{}' [{}]", resourceID, SystemError(hr));
        return hr;
    }
    {
        ScopedLock sl(m_cs);
        m_hInstance = hInstance;
        std::swap(m_hFileResource, hFileResource);
        std::swap(m_hResource, hResource);
    }

    return MemoryStream::OpenForReadOnly(lpFile, dwSize);
}

HRESULT ResourceStream::OpenForReadOnly(__in const HINSTANCE hInstance, __in HRSRC hRes)
{
    HGLOBAL hFileResource = NULL;

    // First find and load the required resource
    if ((hFileResource = LoadResource(hInstance, hRes)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LoadResource [{}]", SystemError(hr));
        return hr;
    }

    // Now open and map this to a disk file
    LPVOID lpFile = NULL;
    if ((lpFile = LockResource(hFileResource)) == NULL)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed LockResource [{}]", SystemError(hr));
        return hr;
    }

    DWORD dwSize = 0L;

    if ((dwSize = SizeofResource(hInstance, hRes)) == 0)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug("Failed SizeofResource [{}]", SystemError(hr));
        return hr;
    }
    {
        ScopedLock sl(m_cs);
        m_hInstance = hInstance;
        std::swap(m_hFileResource, hFileResource);
        std::swap(m_hResource, hRes);
    }

    return MemoryStream::OpenForReadOnly(lpFile, dwSize);
}

HRESULT Orc::ResourceStream::OpenForReadOnly(const std::wstring& strResourceSpec)
{
    using namespace std::string_view_literals;

    if (!EmbeddedResource::IsResourceBased(strResourceSpec.c_str()))
    {
        Log::Error(L"Invalid resource reference '{}'", strResourceSpec);
        return E_FAIL;
    }

    std::wstring ResName, MotherShip, NameInArchive, Format;
    if (auto hr = EmbeddedResource::SplitResourceReference(strResourceSpec, MotherShip, ResName, NameInArchive, Format);
        FAILED(hr))
        return hr;

    if (Format != L"res"sv)
    {
        Log::Error(L"Invalid resource format '{}'. Only the res:# format is currently supported", Format);
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

STDMETHODIMP Orc::ResourceStream::Clone(std::shared_ptr<ByteStream>& clone)
{
    auto new_stream = std::make_shared<ResourceStream>();

    if (IsOpen() == S_FALSE)
    {
        clone = new_stream;
        return S_OK;
    }

    if (auto hr = new_stream->OpenForReadOnly(m_hInstance, m_hResource); FAILED(hr))
        return hr;

    ULONG64 ullCurPos = 0LLU;
    if (auto hr = SetFilePointer(0LL, FILE_CURRENT, &ullCurPos); FAILED(hr))
        return hr;

    ULONG64 ullDupCurPos = 0LLU;
    if (auto hr = new_stream->SetFilePointer(ullCurPos, FILE_BEGIN, &ullDupCurPos); FAILED(hr))
        return hr;
    clone = new_stream;
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
