#pragma once

#include <system_error>
#include <string>
#include <vector>

struct CapsuleOptions;
struct CommandDescriptor;

[[nodiscard]] std::error_code HandleGetRunner(const CapsuleOptions& args);

/// @brief Return the descriptor that registers the 'get' subcommand.
[[nodiscard]] CommandDescriptor GetGetCommandDescriptor();
