#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <system_error>
#include <filesystem>
#include <optional>
#include <map>

namespace fs = std::filesystem;

struct CapsuleOptions;

// Basic resource loading functions
[[nodiscard]] std::error_code
LoadResourceData(HMODULE hModule, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output);

[[nodiscard]] std::error_code
LoadResourceData(const fs::path& path, const std::wstring& name, LPCWSTR type, std::vector<uint8_t>& output);

[[nodiscard]] std::error_code UpdateResourceData(
    const fs::path& exePath,
    const std::wstring& name,
    LPCWSTR type,
    const std::vector<uint8_t>& resource);

[[nodiscard]] std::error_code RemoveResource(const fs::path& path, const std::wstring& name, LPCWSTR type);

[[nodiscard]] std::error_code GetResourceString(HINSTANCE hInstance, UINT uID, std::wstring& string);

[[nodiscard]] std::error_code ProbeResourceSlot(const std::wstring& resourceName, bool& present, DWORD& compressedSize);

// Resource handler functions
[[nodiscard]] std::error_code HandleResourceList(const CapsuleOptions& opts);
[[nodiscard]] std::error_code HandleResourceGet(const CapsuleOptions& opts);
[[nodiscard]] std::error_code HandleResourceSet(const CapsuleOptions& opts);
[[nodiscard]] std::error_code HandleResourceHexdump(const CapsuleOptions& opts);

// Helper functions for resource operations
[[nodiscard]] std::optional<ULONG_PTR> ParseResourceId(const std::wstring& input);
[[nodiscard]] std::wstring GetResourceTypeName(ULONG_PTR id);
[[nodiscard]] std::wstring FormatResourceIdentifier(LPCWSTR lpName);
[[nodiscard]] std::wstring FormatHexDump(const std::vector<uint8_t>& data, size_t maxBytes = 16);
