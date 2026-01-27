//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utilities/Resource.h"

#include <cassert>

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"

#include "Utilities/Compression.h"
#include "Utilities/File.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

namespace {

std::basic_string_view<uint8_t> ToStringView(const std::vector<uint8_t>& buffer)
{
    return {buffer.data(), buffer.size()};
}

}  // namespace

std::error_code LoadResourceData(HMODULE hModule, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output)
{
    std::error_code ec;

    HRSRC hResource = FindResourceW(hModule, name.c_str(), type);
    if (!hResource)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed FindResourceW (name: {}) [{}]", name, ec);
        return ec;
    }

    HGLOBAL hLoaded = LoadResource(hModule, hResource);
    if (!hLoaded)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed LoadResource (name: {}, type: {}) [{}]", name, type, ec);
        return ec;
    }

    DWORD size = SizeofResource(hModule, hResource);
    if (size == 0)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed SizeofResource (name: {}) [{}]", name, type, ec);
        return ec;
    }

    const void* pData = LockResource(hLoaded);
    if (!pData)
    {
        return std::make_error_code(std::errc::bad_address);
    }

    const auto* bytePtr = static_cast<const uint8_t*>(pData);
    output.assign(bytePtr, bytePtr + size);
    return {};
}

std::error_code
LoadResourceData(const fs::path& path, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output)
{
    std::error_code ec;

    ScopedModule module(
        LoadLibraryExW(path.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    if (!module)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed LoadLibraryExW (path: {}) [{}]", path, ec);
        return ec;
    }

    ec = LoadResourceData(module.get(), name, type, output);
    if (ec)
    {
        Log::Debug(L"Failed LoadResourceData (path: {}) [{}]", path, ec);
        return ec;
    }

    return {};
}

[[nodiscard]] std::error_code ExtractResource(const std::wstring& name, const std::wstring& outputPath, bool overwrite)
{
    std::error_code ec;

    std::vector<uint8_t> data;
    ec = LoadResourceData(NULL, name, RT_RCDATA, data);
    if (ec)
    {
        Log::Debug(L"Failed LoadResourceData (name: {}) [{}]", name, ec);
        return ec;
    }

    if (data.empty())
    {
        Log::Debug(L"Failed LoadResourceData: resource empty (name: {})", name);
        ec.assign(ERROR_RESOURCE_NAME_NOT_FOUND, std::system_category());
        return ec;
    }

    bool exists = fs::exists(outputPath, ec);
    if (ec)
    {
        Log::Debug(L"Failed fs::exists (resource: {}, path: {}) [{}]", name, outputPath, ec);
        return ec;
    }

    if (exists && !overwrite)
    {
        Log::Error(L"Output file exists. Use -f to overwrite (output: {})", outputPath);
        return std::make_error_code(std::errc::file_exists);
    }

    std::vector<uint8_t> decompressedData;
    if (IsZstdArchive(ToStringView(data)))
    {
        ec = ZstdDecompressData(data, decompressedData);
        if (ec)
        {
            Log::Debug(L"Failed to decompress data (resource: {}, path: {}) [{}]", name, outputPath, ec);
            return std::make_error_code(std::errc::bad_message);
        }

        if (decompressedData.empty())
        {
            Log::Debug(L"Failed DecompressData: empty data (resource: {}, path: {})", name, outputPath);
            return std::make_error_code(std::errc::bad_message);
        }
    }
    else
    {
        decompressedData = std::move(data);
    }

    ec = WriteFileContents(outputPath, decompressedData);
    if (ec)
    {
        Log::Debug(L"Failed WriteFileContents (resource: {}, path: {}) [{}]", name, outputPath, ec);
        return ec;
    }

    return {};
}

std::error_code GetResourceString(HINSTANCE hInstance, UINT uID, std::wstring& string)
{
    wchar_t* pString = nullptr;

    // Passing 0 as the buffer size returns a pointer directly into the resource data (read-only).
    int length = LoadStringW(hInstance, uID, reinterpret_cast<LPWSTR>(&pString), 0);
    DWORD lastError = GetLastError();
    if (lastError != ERROR_SUCCESS)
    {
        auto ec = std::error_code(lastError, std::system_category());
        Log::Debug(L"Failed LoadStringW (id: {}) [{}]", uID, ec);
        return ec;
    }

    if (length == 0 || pString == nullptr)
    {
        Log::Debug(L"Failed LoadStringW (id: {}): empty result", uID);
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    string = std::wstring(pString, length);
    return {};
}

std::error_code UpdateResourceData(
    const fs::path& exePath,
    const std::wstring& name,
    LPCWSTR type,
    const std::vector<uint8_t>& resource)
{
    std::unique_ptr<ResourceUpdater> updater;
    auto ec = ResourceUpdater::Make(exePath, updater);
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Make (path: {}) [{}]", exePath, ec);
        return ec;
    }

    ec = updater->Update(type, name, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), resource);
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Update (path: {}, name: {}) [{}]", exePath, name, ec);
        return ec;
    }

    ec = updater->Commit();
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Commit (path: {}) [{}]", exePath, ec);
        return ec;
    }

    return {};
}

std::error_code RemoveResource(const fs::path& path, const std::wstring& name, LPCWSTR type)
{
    std::error_code ec;

    std::vector<uint8_t> empty;
    ec = UpdateResourceData(path, name, type, empty);
    if (ec)
    {
        Log::Error(L"Failed UpdateResourceData (path: {}, resource: {}) [{}]", path, name, ec);
        return ec;
    }

    return {};
}

std::error_code ResourceUpdater::Make(std::filesystem::path path, std::unique_ptr<ResourceUpdater>& resourceUpdater)
{
    HANDLE hUpdate = BeginUpdateResourceW(path.c_str(), FALSE);
    if (!hUpdate)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed BeginUpdateResourceW (path: {}) [{}]", path, ec);
        return ec;
    }

    resourceUpdater = std::unique_ptr<ResourceUpdater>(new ResourceUpdater(std::move(path), hUpdate));
    return {};
}

ResourceUpdater::~ResourceUpdater()
{
    if (m_handle)
    {
        Log::Warn(L"Resource updater was not closed properly (file: {})", m_path);

        if (!EndUpdateResourceW(m_handle, FALSE))
        {
            Log::Debug(L"Failed EndUpdateResourceW for best effort cleanup (path: {}) [{}]", m_path, LastWin32Error());
        }

        m_handle = nullptr;
    }
}

ResourceUpdater::ResourceUpdater(ResourceUpdater&& other) noexcept
    : m_handle {other.m_handle}
{
    other.m_handle = nullptr;
    other.m_path.clear();
}

ResourceUpdater& ResourceUpdater::operator=(ResourceUpdater&& other) noexcept
{
    if (this != &other)
    {
        if (m_handle)
        {
            if (!EndUpdateResourceW(m_handle, FALSE))
            {
                Log::Debug(
                    L"Failed EndUpdateResourceW for best effor cleanup (path: {}) [{}]", LastWin32Error(), m_path);
            }
        }

        m_handle = other.m_handle;
        m_path = std::move(other.m_path);
        other.m_handle = nullptr;
    }

    return *this;
}

bool ResourceUpdater::isValid() const
{
    return m_handle != nullptr;
}

std::error_code
ResourceUpdater::Update(LPCWSTR type, const std::wstring& name, WORD langId, const std::vector<uint8_t>& data)
{
    assert(isValid() && "Update called on an invalid ResourceUpdater");

    // UpdateResourceW takes LPVOID but does not modify the buffer
    auto* dataPtr = const_cast<uint8_t*>(data.empty() ? nullptr : data.data());
    const auto dataSize = static_cast<DWORD>(data.size());

    if (!UpdateResourceW(m_handle, type, name.c_str(), langId, dataPtr, dataSize))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed UpdateResourceW (path: {}) [{}]", m_path, ec);
        return ec;
    }

    return {};
}

std::error_code ResourceUpdater::Commit()
{
    assert(isValid() && "Commit called on an invalid ResourceUpdater");

    HANDLE hUpdate = m_handle;
    m_handle = nullptr;

    if (!EndUpdateResourceW(hUpdate, FALSE))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed EndUpdateResourceW (path: {}) [{}]", m_path, ec);
        return ec;
    }

    return {};
}

ResourceUpdater::ResourceUpdater(std::filesystem::path path, HANDLE handle)
    : m_path(std::move(path))
    , m_handle(handle)
{
}
