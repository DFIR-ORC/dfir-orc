//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>

#include "Utilities/Manifest.h"

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

/// @brief What BrandCapsuleFromBinary should transplant.
struct BrandingRequest
{
    bool icon = true;
    bool version = true;

    /// UAC level to write into the capsule's own manifest. Empty leaves the capsule's level alone, which
    /// is what a payload that declares no requestedExecutionLevel gets.
    std::optional<ExecutionLevel> manifestLevel;
};

/// @brief What BrandCapsuleFromBinary actually transplanted.
struct BrandingResult
{
    bool iconApplied = false;
    bool versionApplied = false;
    bool manifestApplied = false;
};

/// @brief Copy the application icon, version resource and/or UAC level from a PE binary into a capsule.
///
/// Each transplant is independent and gated on the source: the capsule's own icon is deleted and replaced
/// only when @p sourceBinary actually carries an icon group, and its RT_VERSION is overwritten only when
/// the source has one. A source lacking a resource leaves the capsule's corresponding resource untouched.
/// The UAC level is handled differently since it is an attribute rather than a whole resource: the
/// caller resolves it (see BrandingRequest::manifestLevel) and only that attribute of the capsule's
/// existing manifest is rewritten, so the capsule keeps its own assembly identity and supportedOS list.
///
/// Everything is staged in a single resource update, so the capsule file is rewritten once.
///
/// @param sourceBinary PE file to read the icon/version from.
/// @param capsulePath  Capsule executable to modify in place.
/// @param request      Which resources to transplant.
/// @param result       Receives what was transplanted.
[[nodiscard]] std::error_code BrandCapsuleFromBinary(
    const std::filesystem::path& sourceBinary,
    const std::filesystem::path& capsulePath,
    const BrandingRequest& request,
    BrandingResult& result);

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

    /// @brief Write the staged updates to the file. This is the only path that commits: an updater that
    /// goes away without Commit() discards everything it staged (and warns), so a failed multi-step
    /// update never leaves a half-rewritten resource table behind.
    [[nodiscard]] std::error_code Commit();

private:
    ResourceUpdater(std::filesystem::path path, HANDLE handle);
    ResourceUpdater() = default;

    void Discard();

private:
    std::filesystem::path m_path;
    HANDLE m_handle {nullptr};
};
