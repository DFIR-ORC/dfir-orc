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

/// @brief Copy the application icon and/or version resource from a PE binary into a capsule executable.
///
/// The icon transplant is all-or-nothing and gated on the source: the capsule's own icon is deleted
/// and replaced only when @p sourceBinary actually carries an icon group; likewise the capsule's
/// RT_VERSION is overwritten only when the source has one. A source lacking either resource leaves the
/// capsule's corresponding resource untouched. Icon and version are handled independently.
///
/// @param sourceBinary   PE file to read the icon/version from.
/// @param capsulePath    Capsule executable to modify in place.
/// @param copyIcon       When true, attempt to transplant the icon.
/// @param copyVersion    When true, attempt to transplant the version resource.
/// @param iconApplied    Set to true when an icon was actually transplanted.
/// @param versionApplied Set to true when a version resource was actually transplanted.
[[nodiscard]] std::error_code BrandCapsuleFromBinary(
    const std::filesystem::path& sourceBinary,
    const std::filesystem::path& capsulePath,
    bool copyIcon,
    bool copyVersion,
    bool& iconApplied,
    bool& versionApplied);

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

    /// @brief Stage an update for a resource identified by a numeric id (e.g. an RT_ICON or RT_VERSION).
    /// @note An empty @p data deletes the resource. Named-string ids must use the wstring overload.
    [[nodiscard]] std::error_code Update(LPCWSTR type, WORD nameId, WORD langId, const std::vector<uint8_t>& data);

    [[nodiscard]] std::error_code Commit();

private:
    ResourceUpdater(std::filesystem::path path, HANDLE handle);
    ResourceUpdater() = default;

private:
    std::filesystem::path m_path;
    HANDLE m_handle {nullptr};
};
