#pragma once

#include <filesystem>
#include <string>
#include <system_error>

enum class Architecture;

const wchar_t* GetRunnerResourceName(Architecture arch);

[[nodiscard]] std::error_code GetRunnerFilename(Architecture arch, std::wstring& filename);

[[nodiscard]] std::error_code
ExtractCompatibleRunner(const std::wstring& outputDirectory, bool force, std::filesystem::path& outputFile);

[[nodiscard]] std::error_code ResolveRunnerArchitectureFromHost(Architecture& arch);
