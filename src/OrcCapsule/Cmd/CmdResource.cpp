//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "CmdResource.h"

#include <array>
#include <map>

#include <windows.h>

#include "OrcCapsuleCli.h"

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Compression.h"
#include "Utilities/File.h"
#include "Utilities/Process.h"
#include "Utilities/Resource.h"
#include "Utilities/Format.h"
#include "Utilities/Strings.h"

#include "Fmt/std_error_code.h"
#include "Fmt/std_filesystem.h"

namespace {

std::basic_string_view<uint8_t> ToStringView(const std::vector<uint8_t>& buffer)
{
    return {buffer.data(), buffer.size()};
}

}  // namespaces

[[nodiscard]] std::error_code HandleResourceSet(const CapsuleOptions& opts)
{
    std::error_code ec;

    // Prepare data
    std::vector<uint8_t> data;

    if (opts.valuePath)
    {
        ec = ReadFileContents(*opts.valuePath, data);
        if (ec)
        {
            Log::Error(L"Failed to read value-path '{}' [{}]", *opts.valuePath, ec);
            return ec;
        }
    }
    else if (opts.valueUtf8)
    {
        data.assign(opts.valueUtf8->begin(), opts.valueUtf8->end());
    }
    else if (opts.valueUtf16)
    {
        const std::wstring& ws = *opts.valueUtf16;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(ws.data());
        data.assign(p, p + ws.size() * sizeof(wchar_t));
    }
    else
    {
        Log::Error(L"No input provided for set-resource");
        return std::make_error_code(std::errc::invalid_argument);
    }

    // Optional compression
    if (opts.compression == Compression::Zstd)
    {
        std::vector<uint8_t> compressed;
        ec = ZstdCompressData(data, compressed);
        if (ec)
        {
            Log::Error(L"Failed to compress resource data [{}]", ec);
            return ec;
        }
        data = std::move(compressed);
    }

    // Determine target executable (copy current exe to .new or use outputPath)
    fs::path currentExe;
    ec = GetCurrentProcessExecutable(currentExe);
    if (ec)
    {
        Log::Error(L"Failed GetCurrentProcessExecutable [{}]", ec);
        return ec;
    }

    fs::path updatedExe =
        opts.outputPath.value_or(currentExe.parent_path() / (currentExe.filename().wstring() + L".new"));

    fs::copy_file(
        currentExe, updatedExe, opts.force ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
    if (ec)
    {
        Log::Error(L"Failed to copy file for update (src: {}, dst: {}) [{}]", currentExe, updatedExe, ec);
        return ec;
    }

    // Update resource
    std::unique_ptr<ResourceUpdater> updater;
    ec = ResourceUpdater::Make(updatedExe, updater);
    if (ec)
    {
        Log::Error(L"Failed ResourceUpdater::Make (path: {}) [{}]", updatedExe, ec);
        return ec;
    }

    WORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    if (opts.resourceLang.has_value())
    {
        langId = static_cast<WORD>(*opts.resourceLang);
    }

    ec = updater->Update(opts.resourceType.c_str(), opts.resourceName, langId, data);
    if (ec)
    {
        Log::Error(
            L"Failed ResourceUpdater::Update (path: {}, resource: {}:{}) [{}]",
            updatedExe,
            opts.resourceType,
            opts.resourceName,
            ec);
        return ec;
    }

    ec = updater->Commit();
    if (ec)
    {
        Log::Error(L"Failed ResourceUpdater::Commit (path: {}) [{}]", updatedExe, ec);
        return ec;
    }

    // If no explicit output was requested, perform self-update swap
    if (!opts.outputPath)
    {
        ec = PerformSelfUpdate(updatedExe);
        if (ec)
        {
            Log::Error(
                L"Failed to perform self-update swap (original: {}, updated: {}) [{}]", currentExe, updatedExe, ec);
            return ec;
        }
    }

    Log::Info(L"Updated resource {}:{} on {}", opts.resourceType, opts.resourceName, updatedExe);
    return {};
}

// Parse user input resource type/name string to LPCWSTR for Windows API
// Handles: "RT_VERSION", "#16", "16", "MY_CUSTOM_TYPE"
[[nodiscard]] std::optional<ULONG_PTR> ParseResourceId(const std::wstring& input)
{
    if (input.empty())
    {
        return std::nullopt;
    }

    // Check for known type names
    static const std::map<std::wstring, ULONG_PTR> knownTypes = {
        {L"RT_CURSOR", 1},      {L"RT_BITMAP", 2},     {L"RT_ICON", 3},          {L"RT_MENU", 4},
        {L"RT_DIALOG", 5},      {L"RT_STRING", 6},     {L"RT_FONTDIR", 7},       {L"RT_FONT", 8},
        {L"RT_ACCELERATOR", 9}, {L"RT_RCDATA", 10},    {L"RT_MESSAGETABLE", 11}, {L"RT_GROUP_CURSOR", 12},
        {L"RT_GROUP_ICON", 14}, {L"RT_VERSION", 16},   {L"RT_DLGINCLUDE", 17},   {L"RT_PLUGPLAY", 19},
        {L"RT_VXD", 20},        {L"RT_ANICURSOR", 21}, {L"RT_ANIICON", 22},      {L"RT_HTML", 23},
        {L"RT_MANIFEST", 24}};

    auto it = knownTypes.find(input);
    if (it != knownTypes.end())
    {
        return it->second;
    }

    // Check for #123 format
    if (!input.empty() && input[0] == L'#')
    {
        try
        {
            unsigned long long value = std::stoull(input.substr(1));
            return static_cast<ULONG_PTR>(value);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    // Check if it's a plain number
    try
    {
        unsigned long long value = std::stoull(input);
        return static_cast<ULONG_PTR>(value);
    }
    catch (...)
    {
        // Not a number, treat as string name
        return std::nullopt;
    }
}

[[nodiscard]] std::error_code HandleResourceGet(const CapsuleOptions& opts)
{
    std::error_code ec;

    if (opts.resourceType.empty() || opts.resourceName.empty())
    {
        Log::Error(L"Both --type and --name are required for 'resource get'");
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (!opts.outputPath || opts.outputPath->empty())
    {
        Log::Error(L"--output is required for 'resource get'");
        return std::make_error_code(std::errc::invalid_argument);
    }

    HMODULE hModule = ::GetModuleHandleW(nullptr);
    const WORD langId = opts.resourceLang.has_value() ? static_cast<WORD>(*opts.resourceLang)
                                                      : MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    // Parse resource type - could be "RT_VERSION", "#16", or "CUSTOM_TYPE"
    auto typeId = ParseResourceId(opts.resourceType);
    LPCWSTR resourceType = typeId ? MAKEINTRESOURCEW(*typeId) : opts.resourceType.c_str();

    // Parse resource name - could be "#123" or "RESOURCE_NAME"
    auto nameId = ParseResourceId(opts.resourceName);
    LPCWSTR resourceName = nameId ? MAKEINTRESOURCEW(*nameId) : opts.resourceName.c_str();

    HRSRC hRes = FindResourceExW(hModule, resourceType, resourceName, langId);
    if (!hRes)
    {
        ec = LastWin32Error();
        Log::Error(
            L"Failed FindResourceExW (type: {}, name: {}, lang: {}) [{}]",
            opts.resourceType,
            opts.resourceName,
            langId,
            ec);
        return ec;
    }

    HGLOBAL hLoaded = LoadResource(hModule, hRes);
    if (!hLoaded)
    {
        ec = LastWin32Error();
        Log::Error(L"Failed LoadResource (type: {}, name: {}) [{}]", opts.resourceType, opts.resourceName, ec);
        return ec;
    }

    DWORD size = SizeofResource(hModule, hRes);
    if (size == 0)
    {
        ec = LastWin32Error();
        Log::Error(L"Failed SizeofResource (type: {}, name: {}) [{}]", opts.resourceType, opts.resourceName, ec);
        return ec;
    }

    const void* pData = LockResource(hLoaded);
    if (!pData)
    {
        Log::Error(L"Failed LockResource (type: {}, name: {})", opts.resourceType, opts.resourceName);
        return std::make_error_code(std::errc::bad_address);
    }

    const auto* bytePtr = static_cast<const uint8_t*>(pData);
    std::vector<uint8_t> data(bytePtr, bytePtr + size);

    // Check if output already exists
    bool exists = fs::exists(*opts.outputPath, ec);
    if (ec)
    {
        Log::Debug(L"Failed fs::exists (path: {}) [{}]", *opts.outputPath, ec);
        return ec;
    }

    if (exists && !opts.force)
    {
        Log::Error(L"Output file exists. Use --force to overwrite (output: {})", *opts.outputPath);
        return std::make_error_code(std::errc::file_exists);
    }

    ec = WriteFileContents(*opts.outputPath, data);
    if (ec)
    {
        Log::Error(L"Failed to write resource to file (path: {}) [{}]", *opts.outputPath, ec);
        return ec;
    }

    Log::Info(L"Extracted resource {}:{} to {}", opts.resourceType, opts.resourceName, *opts.outputPath);
    return {};
}

[[nodiscard]] std::error_code HandleResourceHexdump(const CapsuleOptions& opts)
{
    std::error_code ec;

    if (opts.resourceType.empty() || opts.resourceName.empty())
    {
        Log::Error(L"Both --type and --name are required for 'resource hexdump'");
        return std::make_error_code(std::errc::invalid_argument);
    }

    HMODULE hModule = ::GetModuleHandleW(nullptr);
    const WORD langId = opts.resourceLang.has_value() ? static_cast<WORD>(*opts.resourceLang)
                                                      : MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    // Parse resource type - could be "RT_VERSION", "#16", or "CUSTOM_TYPE"
    auto typeId = ParseResourceId(opts.resourceType);
    LPCWSTR resourceType = typeId ? MAKEINTRESOURCEW(*typeId) : opts.resourceType.c_str();

    // Parse resource name - could be "#123" or "RESOURCE_NAME"
    auto nameId = ParseResourceId(opts.resourceName);
    LPCWSTR resourceName = nameId ? MAKEINTRESOURCEW(*nameId) : opts.resourceName.c_str();

    HRSRC hRes = FindResourceExW(hModule, resourceType, resourceName, langId);
    if (!hRes)
    {
        ec = LastWin32Error();
        Log::Error(
            L"Failed FindResourceExW (type: {}, name: {}, lang: {}) [{}]",
            opts.resourceType,
            opts.resourceName,
            langId,
            ec);
        return ec;
    }

    HGLOBAL hLoaded = LoadResource(hModule, hRes);
    if (!hLoaded)
    {
        ec = LastWin32Error();
        Log::Error(L"Failed LoadResource (type: {}, name: {}) [{}]", opts.resourceType, opts.resourceName, ec);
        return ec;
    }

    DWORD size = SizeofResource(hModule, hRes);
    if (size == 0)
    {
        ec = LastWin32Error();
        Log::Error(L"Failed SizeofResource (type: {}, name: {}) [{}]", opts.resourceType, opts.resourceName, ec);
        return ec;
    }

    const void* pData = LockResource(hLoaded);
    if (!pData)
    {
        Log::Error(L"Failed LockResource (type: {}, name: {})", opts.resourceType, opts.resourceName);
        return std::make_error_code(std::errc::bad_address);
    }

    // Print header
    fmt::print(stdout, L"Resource: {}:{} (lang: {})\n", opts.resourceType, opts.resourceName, langId);
    fmt::print(stdout, L"Size: {} ({} bytes)\n\n", FormatHumanSize(size), size);

    // Detect zstd compression
    std::basic_string_view<uint8_t> data(static_cast<const uint8_t*>(pData), size);
    if (IsZstdArchive(data))
    {
        double ratio = 0;
        uint64_t frameSize = 0;
        ec = ZstdGetCompressRatio(data, frameSize, ratio);
        if (ec)
        {
            Log::Debug(L"Failed ZstdGetCompressRatio [{}]", ec);
        }
        else
        {
            fmt::print(
                stdout,
                L"Detected zstd compression: {} -> {} ({:.1f}%)\n\n",
                FormatHumanSize(frameSize),
                FormatHumanSize(size),
                ratio);
        }
    }

    // Print hexdump
    constexpr size_t bytesPerLine = 16;
    for (size_t offset = 0; offset < size; offset += bytesPerLine)
    {
        // Print offset
        fmt::print(stdout, L"{:08X}  ", offset);

        // Print hex bytes
        size_t lineEnd = std::min(offset + bytesPerLine, static_cast<size_t>(size));
        for (size_t i = offset; i < lineEnd; ++i)
        {
            fmt::print(stdout, L"{:02X} ", data[i]);
            if (i == offset + 7 && lineEnd > offset + 8)
            {
                fmt::print(stdout, L" ");  // Extra space in the middle
            }
        }

        // Pad if incomplete line
        if (lineEnd < offset + bytesPerLine)
        {
            size_t missing = offset + bytesPerLine - lineEnd;
            for (size_t i = 0; i < missing; ++i)
            {
                fmt::print(stdout, L"   ");
            }
            if (lineEnd <= offset + 8)
            {
                fmt::print(stdout, L" ");
            }
        }

        // Print ASCII representation
        fmt::print(stdout, L" |");
        for (size_t i = offset; i < lineEnd; ++i)
        {
            wchar_t ch = static_cast<wchar_t>(data[i]);
            if (ch >= 32 && ch <= 126)
            {
                fmt::print(stdout, L"{}", ch);
            }
            else
            {
                fmt::print(stdout, L".");
            }
        }
        fmt::print(stdout, L"|\n");
    }

    fmt::print(stdout, L"\n");
    return {};
}

// Well-known Windows resource type IDs
[[nodiscard]] std::wstring GetResourceTypeName(ULONG_PTR id)
{
    switch (id)
    {
        case 1:
            return L"RT_CURSOR";
        case 2:
            return L"RT_BITMAP";
        case 3:
            return L"RT_ICON";
        case 4:
            return L"RT_MENU";
        case 5:
            return L"RT_DIALOG";
        case 6:
            return L"RT_STRING";
        case 7:
            return L"RT_FONTDIR";
        case 8:
            return L"RT_FONT";
        case 9:
            return L"RT_ACCELERATOR";
        case 10:
            return L"RT_RCDATA";
        case 11:
            return L"RT_MESSAGETABLE";
        case 12:
            return L"RT_GROUP_CURSOR";
        case 14:
            return L"RT_GROUP_ICON";
        case 16:
            return L"RT_VERSION";
        case 17:
            return L"RT_DLGINCLUDE";
        case 19:
            return L"RT_PLUGPLAY";
        case 20:
            return L"RT_VXD";
        case 21:
            return L"RT_ANICURSOR";
        case 22:
            return L"RT_ANIICON";
        case 23:
            return L"RT_HTML";
        case 24:
            return L"RT_MANIFEST";
        default:
            return L"";
    }
}

[[nodiscard]] std::wstring FormatResourceIdentifier(LPCWSTR lpName)
{
    if (IS_INTRESOURCE(lpName))
    {
        auto id = reinterpret_cast<ULONG_PTR>(lpName);
        auto knownName = GetResourceTypeName(id);
        if (!knownName.empty())
        {
            return fmt::format(L"{} (#{}) ", knownName, id);
        }
        return fmt::format(L"#{}", id);
    }
    return std::wstring(lpName);
}

// Format first N bytes as hex dump with ASCII representation
[[nodiscard]] std::wstring FormatHexDump(const std::vector<uint8_t>& data, size_t maxBytes = 16)
{
    if (data.empty())
    {
        return L"<empty>";
    }

    const size_t bytesToShow = std::min(data.size(), maxBytes);
    std::wstring result;
    result.reserve(bytesToShow * 3 + 20);  // hex + spaces + ASCII

    // Hex part
    for (size_t i = 0; i < bytesToShow; ++i)
    {
        result += fmt::format(L"{:02X}", data[i]);
        if (i < bytesToShow - 1)
        {
            result += (i % 8 == 7) ? L"  " : L" ";
        }
    }

    // Separator
    result += L"  |";

    // ASCII part
    for (size_t i = 0; i < bytesToShow; ++i)
    {
        wchar_t ch = static_cast<wchar_t>(data[i]);
        if (ch >= 32 && ch <= 126)
        {
            result += ch;
        }
        else
        {
            result += L'.';
        }
    }
    result += L"|";

    if (data.size() > maxBytes)
    {
        result += L"...";
    }

    return result;
}

// Callback for EnumResourceNamesW. Collects resource names into a vector passed via lParam.
static BOOL CALLBACK EnumResourceNamesCallback(HMODULE, LPCWSTR, LPWSTR lpName, LONG_PTR lParam)
{
    auto vec = reinterpret_cast<std::vector<std::wstring>*>(lParam);
    if (!vec)
        return FALSE;

    vec->push_back(FormatResourceIdentifier(lpName));
    return TRUE;
}

// Callback for EnumResourceTypesW. Collects resource types into a vector passed via lParam.
static BOOL CALLBACK EnumResourceTypesCallback(HMODULE, LPWSTR lpType, LONG_PTR lParam)
{
    auto vec = reinterpret_cast<std::vector<std::wstring>*>(lParam);
    if (!vec)
        return FALSE;

    vec->push_back(FormatResourceIdentifier(lpType));
    return TRUE;
}

// Callback for EnumResourceLanguagesW. Collects LANGIDs into a vector passed via lParam.
static BOOL CALLBACK EnumResourceLanguagesCallback(HMODULE, LPCWSTR, LPCWSTR, WORD wLanguage, LONG_PTR lParam)
{
    auto vec = reinterpret_cast<std::vector<WORD>*>(lParam);
    if (!vec)
        return FALSE;

    vec->push_back(wLanguage);
    return TRUE;
}

// Helper structure to track resource name details
struct ResourceNameInfo
{
    std::wstring displayName;
    std::wstring rawName;
    std::optional<ULONG_PTR> numericId;
};

[[nodiscard]] ResourceNameInfo ParseResourceName(LPCWSTR lpName)
{
    ResourceNameInfo info;

    if (IS_INTRESOURCE(lpName))
    {
        auto id = reinterpret_cast<ULONG_PTR>(lpName);
        info.numericId = id;
        // For resource names (not types), just display the numeric ID
        info.displayName = fmt::format(L"#{}", id);
        info.rawName = info.displayName;
    }
    else
    {
        info.displayName = std::wstring(lpName);
        info.rawName = info.displayName;
    }

    return info;
}

// Callback to collect raw resource names (not formatted)
struct RawResourceName
{
    std::wstring formatted;  // Formatted display name (e.g., "RT_ICON (#3)" or "#1" or "BINARY")
    std::wstring nameString;  // Actual string name (for string-based resources)
    std::optional<ULONG_PTR> numericId;  // Numeric ID (for integer-based resources)

    // Get a valid LPCWSTR pointer for Windows API calls
    LPCWSTR GetRawPointer() const
    {
        if (numericId.has_value())
        {
            return MAKEINTRESOURCEW(*numericId);
        }
        return nameString.c_str();
    }
};

static BOOL CALLBACK EnumResourceNamesRawCallback(HMODULE, LPCWSTR, LPWSTR lpName, LONG_PTR lParam)
{
    auto vec = reinterpret_cast<std::vector<RawResourceName>*>(lParam);
    if (!vec)
        return FALSE;

    RawResourceName entry;
    entry.formatted = FormatResourceIdentifier(lpName);

    // Properly store the resource name data (not just a pointer)
    if (IS_INTRESOURCE(lpName))
    {
        // It's an integer ID - store the numeric value
        entry.numericId = reinterpret_cast<ULONG_PTR>(lpName);
    }
    else
    {
        // It's a string name - copy the string
        entry.nameString = lpName;
    }

    vec->push_back(std::move(entry));
    return TRUE;
}

[[nodiscard]] std::error_code HandleResourceList(const CapsuleOptions& opts)
{
    HMODULE hModule = ::GetModuleHandleW(nullptr);

    if (opts.resourceType.empty())
    {
        // List ALL resources from ALL types
        struct RawResourceType
        {
            std::wstring formatted;  // Formatted display name (e.g., "RT_ICON (#3)")
            std::wstring nameString;  // Actual string name (for string-based types)
            std::optional<ULONG_PTR> numericId;  // Numeric ID (for integer-based types)

            // Get a valid LPCWSTR pointer for Windows API calls
            LPCWSTR GetRawPointer() const
            {
                if (numericId.has_value())
                {
                    return MAKEINTRESOURCEW(*numericId);
                }
                return nameString.c_str();
            }
        };

        // Callback to collect raw resource types
        auto EnumResourceTypesRawCallback = [](HMODULE, LPWSTR lpType, LONG_PTR lParam) -> BOOL {
            auto vec = reinterpret_cast<std::vector<RawResourceType>*>(lParam);
            if (!vec)
                return FALSE;

            RawResourceType entry;
            entry.formatted = FormatResourceIdentifier(lpType);

            // Properly store the resource type data (not just a pointer)
            if (IS_INTRESOURCE(lpType))
            {
                // It's an integer ID - store the numeric value
                entry.numericId = reinterpret_cast<ULONG_PTR>(lpType);
            }
            else
            {
                // It's a string name - copy the string
                entry.nameString = lpType;
            }

            vec->push_back(std::move(entry));
            return TRUE;
        };

        std::vector<RawResourceType> typesRaw;
        if (!EnumResourceTypesW(hModule, EnumResourceTypesRawCallback, reinterpret_cast<LONG_PTR>(&typesRaw)))
        {
            auto ec = LastWin32Error();
            if (ec.value() == ERROR_RESOURCE_TYPE_NOT_FOUND)
            {
                fmt::print(stdout, L"No resources found\n");
                return {};
            }

            Log::Error(L"Failed EnumResourceTypesW [{}]", ec);
            return ec;
        }

        fmt::print(stdout, L"All resources in capsule:\n\n");

        size_t totalResources = 0;

        // For each type, enumerate and display its resources
        for (const auto& typeEntry : typesRaw)
        {
            std::vector<RawResourceName> namesRaw;
            if (!EnumResourceNamesW(
                    hModule,
                    typeEntry.GetRawPointer(),
                    EnumResourceNamesRawCallback,
                    reinterpret_cast<LONG_PTR>(&namesRaw)))
            {
                continue;  // Skip types with no resources
            }

            if (namesRaw.empty())
            {
                continue;
            }

            // Print type header
            fmt::print(stdout, L"\n{} ({} resources):\n", typeEntry.formatted, namesRaw.size());
            fmt::print(
                stdout,
                L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
                L"Name",
                L"Name ID",
                L"Lang",
                L"Lang ID",
                L"Size",
                L"Compression",
                L"Data Preview");
            fmt::print(stdout, L"{}\n", std::wstring(152, L'-'));

            totalResources += namesRaw.size();

            // Display each resource
            for (const auto& nameEntry : namesRaw)
            {
                auto nameInfo = ParseResourceName(nameEntry.GetRawPointer());

                std::vector<WORD> langs;
                if (!EnumResourceLanguagesW(
                        hModule,
                        typeEntry.GetRawPointer(),
                        nameEntry.GetRawPointer(),
                        EnumResourceLanguagesCallback,
                        reinterpret_cast<LONG_PTR>(&langs)))
                {
                    continue;
                }

                for (WORD lang : langs)
                {
                    HRSRC hRes = FindResourceExW(hModule, typeEntry.GetRawPointer(), nameEntry.GetRawPointer(), lang);
                    if (!hRes)
                    {
                        continue;
                    }

                    DWORD resSize = SizeofResource(hModule, hRes);
                    std::vector<uint8_t> data;

                    if (resSize > 0)
                    {
                        HGLOBAL hLoaded = LoadResource(hModule, hRes);
                        if (hLoaded)
                        {
                            void* pData = LockResource(hLoaded);
                            if (pData)
                            {
                                const auto* ptr = static_cast<const uint8_t*>(pData);
                                data.assign(ptr, ptr + resSize);
                            }
                        }
                    }

                    std::wstring hexDump = FormatHexDump(data, 16);

                    // Detect compression format
                    std::wstring sizeStr = FormatHumanSize(resSize);
                    std::wstring compressionStr = L"-";

                    // Check for 7z archive
                    if (Is7zArchive(ToStringView(data)))
                    {
                        compressionStr = L"7z";
                    }
                    // Check for zstd compression
                    else if (IsZstdArchive(ToStringView(data)))
                    {
                        double ratio = 0;
                        uint64_t frameSize = 0;
                        auto ec = ZstdGetCompressRatio(ToStringView(data), frameSize, ratio);
                        if (ec)
                        {
                            Log::Debug(L"Failed to get zstd compression ratio [{}]", ec);
                            ec.clear();
                            compressionStr = L"zstd";
                        }
                        else
                        {
                            compressionStr = fmt::format(L"zstd {:.1f}%", ratio);
                        }
                    }

                    std::wstring nameIdStr = nameInfo.numericId ? fmt::format(L"#{}", *nameInfo.numericId) : L"-";
                    std::wstring langDisplay = (lang == 0) ? L"Neutral" : fmt::format(L"{:04X}", lang);
                    std::wstring langIdStr = fmt::format(L"{}", lang);

                    fmt::print(
                        stdout,
                        L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
                        nameInfo.displayName,
                        nameIdStr,
                        langDisplay,
                        langIdStr,
                        sizeStr,
                        compressionStr,
                        hexDump);
                }
            }
        }

        fmt::print(stdout, L"\n\nTotal: {} resources across {} types\n\n", totalResources, typesRaw.size());
        return {};
    }

    // Parse resource type - could be "RT_VERSION", "#16", or "CUSTOM_TYPE"
    auto typeId = ParseResourceId(opts.resourceType);
    LPCWSTR resourceType = typeId ? MAKEINTRESOURCEW(*typeId) : opts.resourceType.c_str();

    // Enumerate names with raw pointers preserved
    std::vector<RawResourceName> namesRaw;
    if (!EnumResourceNamesW(hModule, resourceType, EnumResourceNamesRawCallback, reinterpret_cast<LONG_PTR>(&namesRaw)))
    {
        auto ec = LastWin32Error();
        if (ec.value() == ERROR_RESOURCE_TYPE_NOT_FOUND || ec.value() == ERROR_RESOURCE_NAME_NOT_FOUND)
        {
            fmt::print(stdout, L"No resources of type '{}' found\n", opts.resourceType);
            return {};
        }

        Log::Error(L"Failed EnumResourceNamesW (type: {}) [{}]", opts.resourceType, ec);
        return ec;
    }

    // Filter by name if specified
    if (!opts.resourceName.empty())
    {
        auto it = std::find_if(namesRaw.begin(), namesRaw.end(), [&](const RawResourceName& rn) {
            return rn.formatted == opts.resourceName;
        });
        if (it == namesRaw.end())
        {
            fmt::print(stdout, L"Resource '{}:{}' not found\n", opts.resourceType, opts.resourceName);
            return {};
        }
        namesRaw = {*it};
    }

    fmt::print(stdout, L"Resources of type '{}' ({} resources):\n\n", opts.resourceType, namesRaw.size());
    fmt::print(
        stdout,
        L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
        L"Name",
        L"Name ID",
        L"Lang",
        L"Lang ID",
        L"Size",
        L"Compression",
        L"Data Preview (first 16 bytes)");
    fmt::print(stdout, L"{}\n", std::wstring(152, L'-'));

    for (const auto& nameEntry : namesRaw)
    {
        auto nameInfo = ParseResourceName(nameEntry.GetRawPointer());

        // Enumerate languages
        std::vector<WORD> langs;
        if (!EnumResourceLanguagesW(
                hModule,
                resourceType,
                nameEntry.GetRawPointer(),
                EnumResourceLanguagesCallback,
                reinterpret_cast<LONG_PTR>(&langs)))
        {
            std::wstring nameIdStr = nameInfo.numericId ? fmt::format(L"#{}", *nameInfo.numericId) : L"-";
            fmt::print(
                stdout,
                L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
                nameInfo.displayName,
                nameIdStr,
                L"-",
                L"-",
                L"-",
                L"-",
                L"<enum failed>");
            continue;
        }

        if (langs.empty())
        {
            HRSRC hRes = FindResourceW(hModule, nameEntry.GetRawPointer(), resourceType);
            DWORD resSize = hRes ? SizeofResource(hModule, hRes) : 0;
            std::wstring nameIdStr = nameInfo.numericId ? fmt::format(L"#{}", *nameInfo.numericId) : L"-";
            fmt::print(
                stdout,
                L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
                nameInfo.displayName,
                nameIdStr,
                L"0",
                L"0",
                FormatHumanSize(resSize),
                L"-",
                L"");
            continue;
        }

        for (WORD lang : langs)
        {
            HRSRC hRes = FindResourceExW(hModule, resourceType, nameEntry.GetRawPointer(), lang);
            if (!hRes)
            {
                std::wstring nameIdStr = nameInfo.numericId ? fmt::format(L"#{}", *nameInfo.numericId) : L"-";
                fmt::print(
                    stdout,
                    L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
                    nameInfo.displayName,
                    nameIdStr,
                    L"-",
                    fmt::format(L"{}", lang),
                    L"-",
                    L"-",
                    L"<not found>");
                continue;
            }

            DWORD resSize = SizeofResource(hModule, hRes);
            std::vector<uint8_t> data;

            if (resSize > 0)
            {
                HGLOBAL hLoaded = LoadResource(hModule, hRes);
                if (hLoaded)
                {
                    void* pData = LockResource(hLoaded);
                    if (pData)
                    {
                        const auto* ptr = static_cast<const uint8_t*>(pData);
                        data.assign(ptr, ptr + resSize);
                    }
                }
            }

            // Generate hex dump of first 16 bytes
            std::wstring hexDump = FormatHexDump(data, 16);

            // Detect compression format
            std::wstring sizeStr = FormatHumanSize(resSize);
            std::wstring compressionStr = L"-";

            if (Is7zArchive(ToStringView(data)))
            {
                compressionStr = L"7z";
            }
            // Check for zstd compression
            else if (IsZstdArchive(ToStringView(data)))
            {
                double ratio = 0;
                uint64_t frameSize = 0;
                auto ec = ZstdGetCompressRatio(ToStringView(data), frameSize, ratio);
                if (ec)
                {
                    Log::Debug(L"Failed to get zstd compression ratio [{}]", ec);
                    ec.clear();
                    compressionStr = L"zstd";
                }
                else
                {
                    compressionStr = fmt::format(L"zstd {:.1f}%", ratio);
                }
            }

            // Build columns
            std::wstring nameIdStr = nameInfo.numericId ? fmt::format(L"#{}", *nameInfo.numericId) : L"-";
            std::wstring langDisplay = (lang == 0) ? L"Neutral" : fmt::format(L"{:04X}", lang);
            std::wstring langIdStr = fmt::format(L"{}", lang);

            fmt::print(
                stdout,
                L"{:<32}  {:>10}  {:>8}  {:>10}  {:>12}  {:>12}  {}\n",
                nameInfo.displayName,
                nameIdStr,
                langDisplay,
                langIdStr,
                sizeStr,
                compressionStr,
                hexDump);
        }
    }

    fmt::print(stdout, L"\n");
    return {};
}

// ============================================================================
// COMMAND REGISTRATION (parse, validate, descriptor)
// ============================================================================

static std::string Utf16ToUtf8(const std::wstring& wstr)
{
    constexpr std::string_view utfError = "<utf-error>";

    if (wstr.empty())
        return {};

    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0)
        return std::string{utfError};

    std::string result;
    result.resize(size_needed);
    if (!WideCharToMultiByte(
            CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &result[0], size_needed, nullptr, nullptr))
    {
        Log::Debug("Failed WideCharToMultiByte conversion [{}]", LastWin32Error());
        return std::string{utfError};
    }

    return result;
}

static Compression ParseCompression(std::wstring_view value)
{
    if (EqualsIgnoreCase(value, L"zstd"))
    {
        return Compression::Zstd;
    }
    return Compression::None;
}

void ParseResourceCommand(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts)
{
    opts.mode = CapsuleMode::Resource;

    if (idx >= args.size())
    {
        opts.errorMsg = L"Missing subcommand for 'resource'. Use: list, get, set, or hexdump";
        opts.mode = CapsuleMode::Error;
        return;
    }

    std::wstring_view subcommand = args[idx];

    if (EqualsIgnoreCase(subcommand, L"list"))
    {
        opts.resourceSubcommand = ResourceSubcommand::List;
        idx++;
    }
    else if (EqualsIgnoreCase(subcommand, L"get"))
    {
        opts.resourceSubcommand = ResourceSubcommand::Get;
        idx++;
    }
    else if (EqualsIgnoreCase(subcommand, L"set"))
    {
        opts.resourceSubcommand = ResourceSubcommand::Set;
        idx++;
    }
    else if (EqualsIgnoreCase(subcommand, L"hexdump"))
    {
        opts.resourceSubcommand = ResourceSubcommand::Hexdump;
        idx++;
    }
    else
    {
        opts.errorMsg = fmt::format(L"Unknown resource subcommand '{}'. Use: list, get, set, or hexdump", subcommand);
        opts.mode = CapsuleMode::Error;
        return;
    }

    // Parse remaining options based on subcommand
    while (idx < args.size())
    {
        if (TryParseGlobalOption(idx, args, opts))
        {
            continue;
        }

        std::wstring_view arg = args[idx];
        auto HasValue = [&]() { return idx + 1 < args.size(); };

        if (IsFlag(arg, L"--type", L"-t") && HasValue())
        {
            opts.resourceType = args[++idx];
        }
        else if (IsFlag(arg, L"--name", L"-n") && HasValue())
        {
            opts.resourceName = args[++idx];
        }
        else if (IsFlag(arg, L"--lang", L"-l") && HasValue())
        {
            try
            {
                opts.resourceLang = static_cast<uint16_t>(std::stoul(args[++idx], nullptr, 0));
            }
            catch (...)
            {
                // Validation will catch the empty/invalid optional
            }
        }
        else if (IsFlag(arg, L"--compress", L"-c") && HasValue())
        {
            opts.compression = ParseCompression(args[++idx]);
        }
        else if (IsFlag(arg, L"--value-path") && HasValue())
        {
            opts.valuePath = args[++idx];
        }
        else if (IsFlag(arg, L"--value") && HasValue())
        {
            std::wstring val = args[++idx];
            opts.valueUtf8 = Utf16ToUtf8(val);
        }
        else if (IsFlag(arg, L"--value-utf16") && HasValue())
        {
            opts.valueUtf16 = args[++idx];
        }
        else if (IsFlag(arg, L"--output", L"-o") || IsFlag(arg, L"/output"))
        {
            if (HasValue())
            {
                opts.outputPath = args[++idx];
            }
        }

        idx++;
    }
}

bool ValidateResourceOptions(CapsuleOptions& opts)
{
    switch (opts.resourceSubcommand)
    {
        case ResourceSubcommand::List:
            // Optional --type to filter; no validation needed
            return true;

        case ResourceSubcommand::Get:
            if (opts.resourceType.empty() || opts.resourceName.empty())
            {
                opts.errorMsg = L"Both --type and --name are required for 'resource get'";
                return opts.valid = false;
            }
            if (!opts.outputPath.has_value() || opts.outputPath->empty())
            {
                opts.errorMsg = L"--output is required for 'resource get'";
                return opts.valid = false;
            }
            return true;

        case ResourceSubcommand::Set:
            if (opts.resourceType.empty() || opts.resourceName.empty())
            {
                opts.errorMsg = L"Both --type and --name are required for 'resource set'";
                return opts.valid = false;
            }
            {
                int inputs = (opts.valuePath ? 1 : 0) + (opts.valueUtf8 ? 1 : 0) + (opts.valueUtf16 ? 1 : 0);
                if (inputs != 1)
                {
                    opts.errorMsg =
                        L"Exactly one of --value-path, --value, or --value-utf16 is required for 'resource set'";
                    return opts.valid = false;
                }
            }
            return true;

        case ResourceSubcommand::Hexdump:
            if (opts.resourceType.empty() || opts.resourceName.empty())
            {
                opts.errorMsg = L"Both --type and --name are required for 'resource hexdump'";
                return opts.valid = false;
            }
            return true;

        default:
            opts.errorMsg = L"Invalid resource subcommand";
            return opts.valid = false;
    }
}

CommandDescriptor GetResourceCommandDescriptor()
{
    return {
        CapsuleMode::Resource,
        L"resource",
        L"   resource list [--type <type>] [--name <name>]\n"
        L"      List Win32 resources in the capsule\n"
        L"      --type <type>   Optional: filter by resource type (e.g., RT_RCDATA, BINARY)\n"
        L"      --name <name>   Optional: filter by specific resource name\n"
        L"\n"
        L"   resource get --type <type> --name <name> [--lang <lang>] --output|-o <path> [--force]\n"
        L"      Extract a resource from the capsule to a file\n"
        L"      --type <type>   Resource type (e.g., RT_RCDATA, BINARY, WOLFLAUNCHER_CONFIG)\n"
        L"      --name <name>   Resource name\n"
        L"      --lang <lang>   Optional: language ID (default: neutral)\n"
        L"      --output|-o     Output file path\n"
        L"      --force         Overwrite if output file exists\n"
        L"\n"
        L"   resource set --type <type> --name <name> (--value-path|--value|--value-utf16) [--lang <lang>] [--compress zstd] [--output|-o <path>]\n"
        L"      Add or update a resource in the capsule\n"
        L"      --type <type>           Resource type (e.g., BINARY, WOLFLAUNCHER_CONFIG)\n"
        L"      --name <name>           Resource name\n"
        L"      --value-path <path>     Read resource bytes from a file\n"
        L"      --value <string>        Use UTF-8 encoded string as resource content\n"
        L"      --value-utf16 <string>  Use UTF-16 encoded string as resource content\n"
        L"      --lang <lang>           Optional: language ID as decimal (e.g., 1033 for en-US)\n"
        L"      --compress zstd         Optional: compress the resource with zstd\n"
        L"      --output|-o <path>      Optional: write to a new file instead of self-update\n",
        ParseResourceCommand,
        ValidateResourceOptions,
        nullptr,
    };
}
