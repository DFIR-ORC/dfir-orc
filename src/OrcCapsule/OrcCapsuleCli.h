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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Utilities/Log.h"
#include "Utilities/Error.h"
#include "Utilities/Scope.h"

#include "Cmd/Architecture.h"

namespace fs = std::filesystem;

// ============================================================================
// ENUMS AND STRUCTURES
// ============================================================================

enum class CapsuleMode
{
    Run,  // Default mode: execute embedded binary
    Add,
    Get,
    Remove,
    Resource,
    Info,
    RunExplicit,  // Explicit execute embedded binary
    Version,
    Help,
    Error
};

enum class Compression
{
    None,
    Zstd
};

enum class ResourceSubcommand
{
    None,
    List,
    Get,
    Set,
    Hexdump
};

struct CapsuleOptions
{
    CapsuleMode mode = CapsuleMode::Run;
    ResourceSubcommand resourceSubcommand = ResourceSubcommand::None;

    // Generic Resource Options
    std::wstring resourceType;
    std::wstring resourceName;
    std::optional<uint16_t> resourceLang;
    Compression compression = Compression::None;

    // Resource Input (Exclusive group)
    std::optional<fs::path> valuePath;
    std::optional<std::string> valueUtf8;
    std::optional<std::wstring> valueUtf16;

    // Add/Get/Remove Options
    Architecture architecture = Architecture::None;
    std::vector<fs::path> inputPaths;  // 'add' command: 1-3 executables (one per architecture slot)
    std::optional<fs::path> outputPath;
    bool force = false;

    // Execution Configuration (global options)
    std::optional<bool> relocate;
    bool wait = true;  // Default: true (unless --no-wait)
    std::optional<fs::path> tempDir;
    std::optional<fs::path> safeTemp;  // For internal use, not set by user directly (derived from tempDir)
    std::optional<Log::Level> logLevel;

    std::vector<std::wstring> normalizedArgs;
    std::vector<std::wstring> rawArgs;

    // Parsing Status
    bool valid = true;
    std::wstring errorMsg;
};

// ============================================================================
// COMMAND REGISTRATION
// ============================================================================

/// @brief Bundles all metadata and entry points for a named subcommand
struct CommandDescriptor
{
    CapsuleMode mode = CapsuleMode::Error;
    std::wstring_view name;
    const wchar_t* usage = nullptr;
    void (*parse)(size_t&, const std::vector<std::wstring>&, CapsuleOptions&) = nullptr;
    bool (*validate)(CapsuleOptions&) = nullptr;
    std::error_code (*handle)(const CapsuleOptions&) = nullptr;
};

// ============================================================================
// ARGUMENT NORMALIZATION FUNCTIONS
// ============================================================================

/// @brief Splits an argument containing '=' or ':' into option and value parts
/// @param arg The argument to split (e.g., "--temp-dir=C:\\path")
/// @param separators List of acceptable separators (default: "=:")
/// @return Optional pair of (option, value) if split successful, nullopt otherwise
/// @note Security: Validates that separator exists and is within bounds
/// @note Performance: Could use string_view references to avoid copies, but readability prioritized
[[nodiscard]] std::optional<std::pair<std::wstring, std::wstring>>
SplitArgumentAtSeparator(std::wstring_view arg, std::wstring_view separators = L"=:");

/// @brief Normalizes command-line arguments to handle both space and '=' separators uniformly
/// @param argc Argument count from main
/// @param argv Argument array from main
/// @return Vector of normalized arguments where '=' separated args are split into two elements
/// @note This preprocessing step converts "/tempdir=path" into {"/tempdir", "path"}
/// @note Performance: Creates a new vector but improves maintainability significantly.
///       For performance-critical version, could parse inline and use string_view.
[[nodiscard]] std::vector<std::wstring> NormalizeArguments(int argc, wchar_t* argv[]);

// ============================================================================
// ARGUMENT PARSING HELPER FUNCTIONS
// ============================================================================

/// @brief Check if argument matches a flag (case-insensitive)
/// @param arg Argument to check
/// @param longFlag Long form flag (e.g., "--version")
/// @param shortFlag Short form flag (e.g., "-v"), empty if none
/// @return True if arg matches longFlag or shortFlag
[[nodiscard]] bool IsFlag(std::wstring_view arg, std::wstring_view longFlag, std::wstring_view shortFlag = L"");

/// @brief Extract path and optional architecture from string like "path:x64"
/// @param input String that may contain path and architecture separated by ':'
/// @return Pair of (path, architecture). Architecture is None if not found or invalid
/// @note Performance: Could use string_view to avoid copies, but readability prioritized
[[nodiscard]] std::pair<std::wstring, Architecture> ParsePathWithArchitecture(std::wstring_view input);

// ============================================================================
// COMMAND PARSING FUNCTIONS
// ============================================================================

/// @brief Parse global options that can appear anywhere in the command line
/// @param idx Current index in args vector (will be incremented if option consumed)
/// @param args Normalized arguments vector
/// @param opts Options structure to populate
/// @return True if the argument at idx was consumed, false otherwise
[[nodiscard]] bool TryParseGlobalOption(size_t& idx, const std::vector<std::wstring>& args, CapsuleOptions& opts);

/// @brief Main command line parser following git-style commands
/// @param args Normalized arguments vector (from NormalizeArguments)
/// @return Populated CapsuleOptions structure
/// @note Supports: capsule [add|get|remove|list|execute|--version|-v|--help|-h] or implicit execution
CapsuleOptions ParseCommandLine(const int argc, const wchar_t* argv[]);

// ============================================================================
// VALIDATION FUNCTIONS
// ============================================================================

/// @brief Validate all command line options based on detected mode
/// @param opts Options to validate (will set errorMsg and valid if invalid)
/// @return True if valid, false otherwise
bool ValidateOptions(CapsuleOptions& opts);

// ============================================================================
// HELP/USAGE PRINTING FUNCTIONS
// ============================================================================

/// @brief Print version information
void PrintVersion();

/// @brief Print usage header with main command line format
/// @param binaryName Name of the executable
/// @param stream Output stream (default: stdout)
void PrintUsageHeader(std::wstring_view binaryName, FILE* stream = stdout);

// Note: detailed printing helpers were removed; PrintUsage provides a
// fixed-format usage block.

/// @brief Print complete usage information
/// @param binaryName Name of the executable
/// @param stream Output stream (default: stdout)
void PrintUsage(std::wstring_view binaryName, FILE* stream = stdout);
