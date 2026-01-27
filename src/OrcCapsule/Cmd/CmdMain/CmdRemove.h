#pragma once

#include <system_error>

struct CapsuleOptions;
struct CommandDescriptor;

[[nodiscard]] std::error_code HandleRemoveRunner(const CapsuleOptions& args);

/// @brief Return the descriptor that registers the 'remove' subcommand.
[[nodiscard]] CommandDescriptor GetRemoveCommandDescriptor();
