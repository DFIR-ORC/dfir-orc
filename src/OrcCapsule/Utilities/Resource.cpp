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

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace fs = std::filesystem;

namespace {

std::basic_string_view<uint8_t> ToStringView(const std::vector<uint8_t>& buffer)
{
    return {buffer.data(), buffer.size()};
}

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

}  // namespace

std::error_code LoadResourceData(HMODULE hModule, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output)
{
    return LoadResourceRaw(hModule, name.c_str(), type, output);
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

std::error_code BrandCapsuleFromBinary(
    const fs::path& sourceBinary,
    const fs::path& capsulePath,
    bool copyIcon,
    bool& iconApplied)
{
    iconApplied = false;

    if (!copyIcon)
    {
        return {};
    }

    // Read what the source binary can offer. Loading as a datafile keeps this read-only and side-effect free.
    ScopedModule source(LoadLibraryExW(
        sourceBinary.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    if (!source)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed LoadLibraryExW for branding source (path: {}) [{}]", sourceBinary, ec);
        return ec;
    }

    // Read the source's whole icon set (every RT_GROUP_ICON and RT_ICON, numeric or named) so the capsule
    // ends up with an exact copy rather than just the primary group.
    std::vector<SourceIconResource> sourceGroups;
    std::vector<SourceIconResource> sourceImages;
    auto ec = ReadAllIcons(source.get(), RT_GROUP_ICON, sourceGroups);
    if (!ec)
    {
        ec = ReadAllIcons(source.get(), RT_ICON, sourceImages);
    }

    if (ec)
    {
        Log::Debug(L"Failed to read icons from branding source (path: {}) [{}]", sourceBinary, ec);
        return {};
    }

    if (sourceGroups.empty() || sourceImages.empty())
    {
        // Nothing to transplant: leave the capsule's own icon untouched.
        Log::Debug(L"No icon in branding source (path: {})", sourceBinary);
        return {};
    }

    // Enumerate the capsule's own icon resources up front, while no update handle is open on the file, so the
    // datafile mapping and BeginUpdateResource never contend for it.
    std::vector<IconRef> staleGroups;
    std::vector<IconRef> staleIcons;
    {
        ScopedModule capsule(LoadLibraryExW(
            capsulePath.native().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
        if (!capsule)
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed LoadLibraryExW for capsule (path: {}) [{}]", capsulePath, ec);
            return ec;
        }

        std::ignore = CollectExistingIconResources(capsule.get(), RT_GROUP_ICON, staleGroups);
        std::ignore = CollectExistingIconResources(capsule.get(), RT_ICON, staleIcons);
    }

    std::unique_ptr<ResourceUpdater> updater;
    ec = ResourceUpdater::Make(capsulePath, updater);
    if (ec)
    {
        Log::Debug(L"Failed ResourceUpdater::Make (path: {}) [{}]", capsulePath, ec);
        return ec;
    }

    constexpr WORD kNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    // Delete every icon the capsule carries (numeric and named), then write the source's verbatim under its
    // own ids and names. Done in one commit, the capsule's icons become an exact copy of the source's with
    // nothing of its own left behind, and there is no window where the shell has no icon to show.
    const std::vector<uint8_t> empty;

    for (const auto& ref : staleGroups)
    {
        ec = ref.numeric ? updater->Update(RT_GROUP_ICON, ref.id, ref.lang, empty)
                         : updater->Update(RT_GROUP_ICON, ref.name, ref.lang, empty);
        if (ec)
        {
            return ec;
        }
    }

    for (const auto& ref : staleIcons)
    {
        ec = ref.numeric ? updater->Update(RT_ICON, ref.id, ref.lang, empty)
                         : updater->Update(RT_ICON, ref.name, ref.lang, empty);
        if (ec)
        {
            return ec;
        }
    }

    for (const auto& image : sourceImages)
    {
        ec = image.numeric ? updater->Update(RT_ICON, image.id, kNeutral, image.data)
                           : updater->Update(RT_ICON, image.name, kNeutral, image.data);
        if (ec)
        {
            return ec;
        }
    }

    for (const auto& group : sourceGroups)
    {
        ec = group.numeric ? updater->Update(RT_GROUP_ICON, group.id, kNeutral, group.data)
                           : updater->Update(RT_GROUP_ICON, group.name, kNeutral, group.data);
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

    iconApplied = true;
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
