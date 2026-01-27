#pragma once

#include <system_error>

struct CapsuleOptions;
struct CommandDescriptor;

[[nodiscard]] std::error_code HandleInfo(const CapsuleOptions& opts);

/// @brief Return the descriptor that registers the 'info' subcommand.
[[nodiscard]] CommandDescriptor GetInfoCommandDescriptor();
