//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utilities/Resource.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"

#include "Utilities/Compression.h"
#include "Utilities/File.h"
#include "Utilities/VersionInfo.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

namespace {

std::basic_string_view<uint8_t> ToStringView(const std::vector<uint8_t>& buffer)
{
    return {buffer.data(), buffer.size()};
}

// An application manifest is embedded under RT_MANIFEST with this id for an executable image.
constexpr WORD kCreateProcessManifestResourceId = 1;

// An icon resource (RT_ICON or RT_GROUP_ICON) is identified by a numeric id or, as some toolchains emit,
// a string name. This carries the identity plus the language under which it was found, for precise deletion.
struct IconRef
{
    bool numeric = false;
    WORD id = 0;
    std::wstring name;
    WORD lang = 0;
};

// An icon resource to transplant: its identity plus its bytes.
struct SourceIconResource
{
    bool numeric = false;
    WORD id = 0;
    std::wstring name;
    std::vector<uint8_t> data;
};

// EnumResourceNamesW target: the numeric ids and string names a resource type is present under.
struct IconNameList
{
    std::vector<WORD> numeric;
    std::vector<std::wstring> named;
};

// EnumResourceNamesW callback: collect both integer-id and string-named resource names.
BOOL CALLBACK CollectIconNamesCallback(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param)
{
    auto* out = reinterpret_cast<IconNameList*>(param);
    if (!out || !name)
    {
        return TRUE;
    }

    if (IS_INTRESOURCE(name))
    {
        out->numeric.push_back(static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name)));
    }
    else
    {
        out->named.emplace_back(name);
    }

    return TRUE;
}

// EnumResourceLanguagesW callback: collect the languages a resource is present in.
BOOL CALLBACK CollectLanguagesCallback(HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR param)
{
    auto* langs = reinterpret_cast<std::vector<WORD>*>(param);
    if (langs)
    {
        langs->push_back(lang);
    }

    return TRUE;
}

// Load a resource's raw bytes by its FindResource name, which is either a string name or a
// MAKEINTRESOURCE numeric id. Shared by the wstring LoadResourceData and the by-id LoadResourceById.
std::error_code LoadResourceRaw(HMODULE module, LPCWSTR name, LPCWSTR type, std::vector<uint8_t>& output)
{
    HRSRC hResource = FindResourceW(module, name, type);
    if (!hResource)
    {
        auto ec = LastWin32Error();
        if (IS_INTRESOURCE(name))
        {
            Log::Debug(L"Failed FindResourceW (id: {}) [{}]", reinterpret_cast<ULONG_PTR>(name), ec);
        }
        else
        {
            Log::Debug(L"Failed FindResourceW (name: {}) [{}]", std::wstring(name), ec);
        }
        return ec;
    }

    HGLOBAL hLoaded = LoadResource(module, hResource);
    if (!hLoaded)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed LoadResource [{}]", ec);
        return ec;
    }

    DWORD size = SizeofResource(module, hResource);
    if (size == 0)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed SizeofResource [{}]", ec);
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

// Load a resource identified by a numeric id (the wstring LoadResourceData overloads only handle named ids).
std::error_code LoadResourceById(HMODULE module, LPCWSTR type, WORD id, std::vector<uint8_t>& output)
{
    return LoadResourceRaw(module, MAKEINTRESOURCEW(id), type, output);
}

// Load every RT_ICON or RT_GROUP_ICON in a module verbatim (numeric and string-named ids), so a source's
// whole icon set can be transplanted rather than just its primary group. The bytes are copied as-is, so the
// group directories keep referencing their image ids unchanged. Returns an empty list when the module has
// none of @p type.
std::error_code ReadAllIcons(HMODULE module, LPCWSTR type, std::vector<SourceIconResource>& out)
{
    IconNameList names;
    if (!EnumResourceNamesW(module, type, CollectIconNamesCallback, reinterpret_cast<LONG_PTR>(&names)))
    {
        // FALSE is also the normal "module has no resources of this type" case, so log at debug.
        Log::Debug(
            L"EnumResourceNamesW (type: {}) returned false [{}]", reinterpret_cast<ULONG_PTR>(type), LastWin32Error());
    }

    for (WORD id : names.numeric)
    {
        SourceIconResource res;
        res.numeric = true;
        res.id = id;
        auto ec = LoadResourceById(module, type, id, res.data);
        if (ec)
        {
            return ec;
        }

        out.push_back(std::move(res));
    }

    for (auto& name : names.named)
    {
        SourceIconResource res;
        res.name = name;
        auto ec = LoadResourceData(module, name, type, res.data);
        if (ec)
        {
            return ec;
        }

        out.push_back(std::move(res));
    }

    return {};
}

// Collect every RT_GROUP_ICON / RT_ICON resource (numeric and string-named, with its languages) currently
// in a module, so the capsule's own icons can be deleted precisely before the source's are written.
std::error_code CollectExistingIconResources(HMODULE module, LPCWSTR type, std::vector<IconRef>& out)
{
    IconNameList names;
    if (!EnumResourceNamesW(module, type, CollectIconNamesCallback, reinterpret_cast<LONG_PTR>(&names)))
    {
        // FALSE is also the normal "module has no resources of this type" case, so log at debug.
        Log::Debug(
            L"EnumResourceNamesW (type: {}) returned false [{}]", reinterpret_cast<ULONG_PTR>(type), LastWin32Error());
    }

    auto collectLangs = [&](bool numeric, WORD id, const std::wstring& name)
    {
        std::vector<WORD> langs;
        LPCWSTR nameArg = numeric ? MAKEINTRESOURCEW(id) : name.c_str();
        EnumResourceLanguagesW(module, type, nameArg, CollectLanguagesCallback, reinterpret_cast<LONG_PTR>(&langs));

        // A resource with no explicitly enumerated language is treated as neutral.
        if (langs.empty())
        {
            langs.push_back(MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
        }

        for (WORD lang : langs)
        {
            IconRef ref;
            ref.numeric = numeric;
            ref.id = id;
            ref.name = name;
            ref.lang = lang;
            out.push_back(std::move(ref));
        }
    };

    for (WORD id : names.numeric)
    {
        collectLangs(true, id, {});
    }

    for (auto& name : names.named)
    {
        collectLangs(false, 0, name);
    }

    return {};
}

// Read the capsule's own application manifest together with the language it is stored under, so the
// patched blob can be written back over the same resource instead of adding a second one.
// Returns errc::no_such_file_or_directory when the module carries no application manifest.
std::error_code ReadApplicationManifest(HMODULE module, std::vector<uint8_t>& manifest, WORD& lang)
{
    std::vector<WORD> langs;
    EnumResourceLanguagesW(
        module,
        RT_MANIFEST,
        MAKEINTRESOURCEW(kCreateProcessManifestResourceId),
        CollectLanguagesCallback,
        reinterpret_cast<LONG_PTR>(&langs));

    if (langs.empty())
    {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    if (langs.size() > 1)
    {
        // An image has a single application manifest; patch the first and leave the rest as they are
        // rather than guess which one the loader would pick.
        Log::Debug(L"Multiple application manifest languages (count: {})", langs.size());
    }

    lang = langs.front();
    return LoadResourceById(module, RT_MANIFEST, kCreateProcessManifestResourceId, manifest);
}

// Resolve the capsule's manifest to the blob that should be written back for @p level.
// Sets @p rewritten to false when the capsule already declares that level, which counts as satisfied but
// needs no write.
std::error_code PatchApplicationManifest(
    HMODULE capsule,
    const fs::path& capsulePath,
    ExecutionLevel level,
    std::vector<uint8_t>& manifest,
    WORD& lang,
    bool& rewritten)
{
    rewritten = false;

    auto ec = ReadApplicationManifest(capsule, manifest, lang);
    if (ec)
    {
        Log::Warn(L"Capsule carries no application manifest to set its UAC level in (path: {})", capsulePath);
        return ec;
    }

    ExecutionLevel current = ExecutionLevel::None;
    std::ignore = ReadRequestedExecutionLevel(manifest, current);
    if (current == level)
    {
        Log::Debug(L"Capsule already requests '{}'", GetExecutionLevelName(level));
        return {};
    }

    ec = SetRequestedExecutionLevel(manifest, level);
    if (ec)
    {
        Log::Warn(
            L"Capsule manifest has no rewritable requestedExecutionLevel (path: {}) [{}]",
            capsulePath,
            ec);
        return ec;
    }

    rewritten = true;
    return {};
}

// What the branding source (the added binary) can offer.
struct BrandingSource
{
    std::vector<SourceIconResource> iconGroups;
    std::vector<SourceIconResource> iconImages;
    bool haveIcon = false;
    std::vector<uint8_t> version;
    bool haveVersion = false;
};

// What the capsule currently carries that branding must replace (its own icons) or patch (its manifest).
struct CapsuleBrandingState
{
    std::vector<IconRef> staleGroups;
    std::vector<IconRef> staleIcons;
    std::vector<uint8_t> patchedManifest;
    WORD manifestLang = 0;
    bool haveManifest = false;
    bool manifestSatisfied = false;
};

// Read the icon set and version resource the source can offer. Loading as a datafile keeps this read-only.
// Best-effort per resource: a missing icon or version leaves the corresponding fields empty rather than
// failing, but a source that cannot be opened at all is an error.
[[nodiscard]] std::error_code
ReadBrandingSource(const fs::path& sourceBinary, const BrandingRequest& request, BrandingSource& out)
{
    ScopedModule source;
    if (auto ec = LoadResourceModule(sourceBinary, source); ec)
    {
        Log::Debug(L"Failed to load branding source (path: {}) [{}]", sourceBinary, ec);
        return ec;
    }

    if (request.icon)
    {
        // Read the source's whole icon set (every RT_GROUP_ICON and RT_ICON, numeric or named) so the
        // capsule ends up with an exact copy rather than just the primary group.
        auto ec = ReadAllIcons(source.get(), RT_GROUP_ICON, out.iconGroups);
        if (!ec)
        {
            ec = ReadAllIcons(source.get(), RT_ICON, out.iconImages);
        }

        if (ec)
        {
            Log::Debug(L"Failed to read icons from branding source (path: {}) [{}]", sourceBinary, ec);
            out.iconGroups.clear();
            out.iconImages.clear();
        }
        else if (out.iconGroups.empty() || out.iconImages.empty())
        {
            Log::Debug(L"No icon in branding source (path: {})", sourceBinary);
        }
        else
        {
            out.haveIcon = true;
        }
    }

    if (request.version)
    {
        // RT_VERSION resources are conventionally stored under id VS_VERSION_INFO (1).
        auto ec = LoadResourceById(source.get(), RT_VERSION, static_cast<WORD>(VS_VERSION_INFO), out.version);
        if (ec)
        {
            Log::Debug(L"No version resource in branding source (path: {}) [{}]", sourceBinary, ec);
        }
        else if (out.version.empty())
        {
            Log::Debug(L"Empty version resource in branding source (path: {})", sourceBinary);
        }
        else
        {
            out.haveVersion = true;
        }
    }

    return {};
}

// Read the capsule's own resources up front, while no update handle is open on the file, so the datafile
// mapping and BeginUpdateResource never contend for it: its icons (to delete) and its manifest (to patch).
[[nodiscard]] std::error_code ReadCapsuleBrandingState(
    const fs::path& capsulePath, const BrandingRequest& request, bool haveIcon, CapsuleBrandingState& out)
{
    ScopedModule capsule;
    if (auto ec = LoadResourceModule(capsulePath, capsule); ec)
    {
        Log::Debug(L"Failed to load capsule resources (path: {}) [{}]", capsulePath, ec);
        return ec;
    }

    if (haveIcon)
    {
        std::ignore = CollectExistingIconResources(capsule.get(), RT_GROUP_ICON, out.staleGroups);
        std::ignore = CollectExistingIconResources(capsule.get(), RT_ICON, out.staleIcons);
    }

    if (request.manifestLevel)
    {
        // A capsule whose manifest cannot be patched is not an error here: PatchApplicationManifest reports
        // why, and manifestSatisfied tells the caller, which decides (the 'add' command refuses the capsule).
        auto ec = PatchApplicationManifest(
            capsule.get(), capsulePath, *request.manifestLevel, out.patchedManifest, out.manifestLang, out.haveManifest);

        out.manifestSatisfied = !ec;
        if (!out.haveManifest)
        {
            out.patchedManifest.clear();
        }
    }

    return {};
}

// Mirror the source's entire icon set into the capsule, within the open updater: delete every icon the
// capsule carries (numeric and named), then write the source's verbatim under its own id or name. Done in
// one commit, the capsule's icons become an exact copy of the source's with nothing of its own left behind.
[[nodiscard]] std::error_code
WriteIconResources(ResourceUpdater& updater, const CapsuleBrandingState& capsuleState, const BrandingSource& source)
{
    constexpr WORD kNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    const std::vector<uint8_t> empty;

    for (const auto& ref : capsuleState.staleGroups)
    {
        auto ec = ref.numeric ? updater.Update(RT_GROUP_ICON, ref.id, ref.lang, empty)
                              : updater.Update(RT_GROUP_ICON, ref.name, ref.lang, empty);
        if (ec)
        {
            return ec;
        }
    }

    for (const auto& ref : capsuleState.staleIcons)
    {
        auto ec = ref.numeric ? updater.Update(RT_ICON, ref.id, ref.lang, empty)
                              : updater.Update(RT_ICON, ref.name, ref.lang, empty);
        if (ec)
        {
            return ec;
        }
    }

    for (const auto& image : source.iconImages)
    {
        auto ec = image.numeric ? updater.Update(RT_ICON, image.id, kNeutral, image.data)
                                : updater.Update(RT_ICON, image.name, kNeutral, image.data);
        if (ec)
        {
            return ec;
        }
    }

    for (const auto& group : source.iconGroups)
    {
        auto ec = group.numeric ? updater.Update(RT_GROUP_ICON, group.id, kNeutral, group.data)
                                : updater.Update(RT_GROUP_ICON, group.name, kNeutral, group.data);
        if (ec)
        {
            return ec;
        }
    }

    return {};
}

// Tag the branded version info and write it. Branding with the payload's version erases DFIR-ORC's identity
// strings by design; leave a small deliberate trace in FileDescription. Best-effort marker: a parse failure
// keeps the payload's version unmarked rather than dropping it.
[[nodiscard]] std::error_code
WriteVersionResource(ResourceUpdater& updater, std::vector<uint8_t>& version, const fs::path& capsulePath)
{
    auto markerEc = AppendFileDescriptionMarker(version, L" (capsule)");
    if (markerEc)
    {
        Log::Debug(L"Failed to tag version info with capsule marker (path: {}) [{}]", capsulePath, markerEc);
    }

    constexpr WORD kNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    return updater.Update(RT_VERSION, static_cast<WORD>(VS_VERSION_INFO), kNeutral, version);
}

}  // namespace

std::error_code LoadResourceModule(const fs::path& path, ScopedModule& module)
{
    module.reset(
        LoadLibraryExW(path.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    if (module)
    {
        return {};
    }

    // XP and 2003 do not know LOAD_LIBRARY_AS_IMAGE_RESOURCE. Retried on any error rather than on the one XP is
    // expected to return: a genuinely unreadable file simply fails twice.
    const auto imageResourceError = LastWin32Error();
    module.reset(LoadLibraryExW(path.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
    if (!module)
    {
        auto ec = LastWin32Error();
        Log::Debug(
            L"Failed LoadLibraryExW (path: {}, as image resource: [{}], as datafile: [{}])",
            path,
            imageResourceError,
            ec);
        return ec;
    }

    Log::Debug(L"Loaded '{}' as a datafile, the image resource load failed [{}]", path, imageResourceError);
    return {};
}

std::error_code LoadResourceData(HMODULE hModule, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output)
{
    return LoadResourceRaw(hModule, name.c_str(), type, output);
}

std::error_code
LoadResourceData(const fs::path& path, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output)
{
    ScopedModule module;
    std::error_code ec = LoadResourceModule(path, module);
    if (ec)
    {
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

namespace {

// Load an RT_RCDATA resource and, if it is a zstd archive, decompress it into its runnable bytes.
[[nodiscard]] std::error_code LoadAndDecompressResource(const std::wstring& name, std::vector<uint8_t>& output)
{
    std::vector<uint8_t> data;
    auto ec = LoadResourceData(NULL, name, RT_RCDATA, data);
    if (ec)
    {
        Log::Debug(L"Failed LoadResourceData (name: {}) [{}]", name, ec);
        return ec;
    }

    if (data.empty())
    {
        Log::Debug(L"Failed LoadResourceData: resource empty (name: {})", name);
        return std::error_code(ERROR_RESOURCE_NAME_NOT_FOUND, std::system_category());
    }

    if (!IsZstdArchive(ToStringView(data)))
    {
        output = std::move(data);
        return {};
    }

    std::vector<uint8_t> decompressedData;
    ec = ZstdDecompressData(data, decompressedData);
    if (ec)
    {
        Log::Debug(L"Failed to decompress data (resource: {}) [{}]", name, ec);
        return std::make_error_code(std::errc::bad_message);
    }

    if (decompressedData.empty())
    {
        Log::Debug(L"Failed DecompressData: empty data (resource: {})", name);
        return std::make_error_code(std::errc::bad_message);
    }

    output = std::move(decompressedData);
    return {};
}

}  // namespace

[[nodiscard]] std::error_code ExtractResource(const std::wstring& name, const std::wstring& outputPath, bool overwrite)
{
    std::error_code ec;

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

    std::vector<uint8_t> data;
    ec = LoadAndDecompressResource(name, data);
    if (ec)
    {
        return ec;
    }

    ec = WriteFileContents(outputPath, data);
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

std::error_code BrandCapsuleFromBinary(
    const fs::path& sourceBinary,
    const fs::path& capsulePath,
    const BrandingRequest& request,
    BrandingResult& result)
{
    result = {};

    if (!request.icon && !request.version && !request.manifestLevel)
    {
        return {};
    }

    // The UAC level does not come from the source read: the caller resolved it into request.manifestLevel.
    BrandingSource source;
    if (request.icon || request.version)
    {
        auto ec = ReadBrandingSource(sourceBinary, request, source);
        if (ec)
        {
            return ec;
        }
    }

    CapsuleBrandingState capsuleState;
    if (source.haveIcon || request.manifestLevel)
    {
        auto ec = ReadCapsuleBrandingState(capsulePath, request, source.haveIcon, capsuleState);
        if (ec)
        {
            return ec;
        }
    }

    if (!source.haveIcon && !source.haveVersion && !capsuleState.haveManifest)
    {
        // Nothing to write: leave the capsule's own resources untouched.
        result.manifestApplied = capsuleState.manifestSatisfied;
        return {};
    }

    // Everything is staged in a single resource update, so the capsule file is rewritten once.
    std::unique_ptr<ResourceUpdater> updater;
    auto ec = ResourceUpdater::Make(capsulePath, updater);
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Make (path: {}) [{}]", capsulePath, ec);
        return ec;
    }

    if (source.haveIcon)
    {
        ec = WriteIconResources(*updater, capsuleState, source);
        if (ec)
        {
            return ec;
        }
    }

    if (source.haveVersion)
    {
        ec = WriteVersionResource(*updater, source.version, capsulePath);
        if (ec)
        {
            return ec;
        }
    }

    if (capsuleState.haveManifest)
    {
        // Written back under the language it was found in, so the loader keeps resolving the same resource
        // instead of finding a second manifest next to the original.
        ec = updater->Update(
            RT_MANIFEST, kCreateProcessManifestResourceId, capsuleState.manifestLang, capsuleState.patchedManifest);
        if (ec)
        {
            return ec;
        }
    }

    ec = updater->Commit();
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Commit for branding (path: {}) [{}]", capsulePath, ec);
        return ec;
    }

    result.iconApplied = source.haveIcon;
    result.versionApplied = source.haveVersion;
    result.manifestApplied = capsuleState.manifestSatisfied;
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
    Discard();
}

// Drop every staged update without touching the file. Reaching this with an open handle means a caller
// bailed out between Make() and Commit(): committing here would write whatever subset was staged (for
// instance deleted icons with no replacement), so discard and say so.
void ResourceUpdater::Discard()
{
    if (!m_handle)
    {
        return;
    }

    Log::Warn(L"Resource updater closed without Commit, discarding pending update (file: {})", m_path);

    if (!EndUpdateResourceW(m_handle, TRUE))
    {
        Log::Debug(L"Failed EndUpdateResourceW while discarding (path: {}) [{}]", m_path, LastWin32Error());
    }

    m_handle = nullptr;
}

ResourceUpdater::ResourceUpdater(ResourceUpdater&& other) noexcept
    : m_path(std::move(other.m_path))
    , m_handle {other.m_handle}
{
    other.m_handle = nullptr;
    other.m_path.clear();
}

ResourceUpdater& ResourceUpdater::operator=(ResourceUpdater&& other) noexcept
{
    if (this != &other)
    {
        Discard();

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

std::error_code ResourceUpdater::Update(LPCWSTR type, WORD nameId, WORD langId, const std::vector<uint8_t>& data)
{
    assert(isValid() && "Update called on an invalid ResourceUpdater");

    // UpdateResourceW takes LPVOID but does not modify the buffer. An empty buffer deletes the resource.
    auto* dataPtr = const_cast<uint8_t*>(data.empty() ? nullptr : data.data());
    const auto dataSize = static_cast<DWORD>(data.size());

    if (!UpdateResourceW(m_handle, type, MAKEINTRESOURCEW(nameId), langId, dataPtr, dataSize))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed UpdateResourceW (path: {}, id: {}) [{}]", m_path, nameId, ec);
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
