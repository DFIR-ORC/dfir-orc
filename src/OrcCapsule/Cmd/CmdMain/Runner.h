#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "Utilities/Scope.h"

enum class Architecture;

const wchar_t* GetRunnerResourceName(Architecture arch);

[[nodiscard]] std::error_code GetRunnerFilename(Architecture arch, std::wstring& filename);

/// @brief Extract the runner matching the host (with architecture fallback) into a delete-on-close file.
///
/// On success @p outputFile receives the runner path and @p keepAlive receives the open handle. The file
/// exists only while @p keepAlive is open: keep it open until the launched runner has exited, then let it
/// close to have the kernel remove the file (no privilege, no reboot; works from Windows XP onward).
[[nodiscard]] std::error_code ExtractCompatibleRunner(
    const std::wstring& outputDirectory, std::filesystem::path& outputFile, ScopedFileHandle& keepAlive);

[[nodiscard]] std::error_code ResolveRunnerArchitectureFromHost(Architecture& arch);
