#pragma once

#include <system_error>

struct CapsuleOptions;
struct CommandDescriptor;

[[nodiscard]] std::error_code HandleAddRunner(const CapsuleOptions& args);

/// @brief Return the descriptor that registers the 'add' subcommand (name,
///        usage string, parse, validate and handle entry points).
[[nodiscard]] CommandDescriptor GetAddCommandDescriptor();
