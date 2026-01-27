//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>

[[nodiscard]] std::error_code
LoadResourceData(HMODULE hModule, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output);

[[nodiscard]] std::error_code LoadResourceData(
    const std::filesystem::path& path,
    const std::wstring& name,
    LPCWSTR type,
    std::vector<uint8_t>& output);

[[nodiscard]] std::error_code ExtractResource(const std::wstring& name, const std::wstring& outputPath, bool overwrite);

[[nodiscard]] std::error_code GetResourceString(HINSTANCE hInstance, UINT uID, std::wstring& string);

[[nodiscard]] std::error_code UpdateResourceData(
    const std::filesystem::path& exePath,
    const std::wstring& name,
    LPCWSTR type,
    const std::vector<uint8_t>& resource);

[[nodiscard]] std::error_code RemoveResource(const std::filesystem::path& path, const std::wstring& name, LPCWSTR type);

class ResourceUpdater final
{
public:
    [[nodiscard]] static std::error_code
    Make(std::filesystem::path path, std::unique_ptr<ResourceUpdater>& resourceUpdater);

    ~ResourceUpdater();

    ResourceUpdater(ResourceUpdater&& other) noexcept;
    ResourceUpdater(const ResourceUpdater&) = delete;
    ResourceUpdater& operator=(ResourceUpdater&& other) noexcept;
    ResourceUpdater& operator=(const ResourceUpdater&) = delete;

    [[nodiscard]] bool isValid() const;

    [[nodiscard]] std::error_code
    Update(LPCWSTR type, const std::wstring& name, WORD langId, const std::vector<uint8_t>& data);

    [[nodiscard]] std::error_code Commit();

private:
    ResourceUpdater(std::filesystem::path path, HANDLE handle);
    ResourceUpdater() = default;

private:
    std::filesystem::path m_path;
    HANDLE m_handle {nullptr};
};
