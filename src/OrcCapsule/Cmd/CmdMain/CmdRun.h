#pragma once

#include <system_error>

#include <windows.h>

struct CapsuleOptions;
struct CommandDescriptor;

[[nodiscard]] std::error_code HandleRun(const CapsuleOptions& opts, DWORD& childExitCode);

/// @brief Return the descriptor that registers the 'execute' subcommand.
[[nodiscard]] CommandDescriptor GetExecuteCommandDescriptor();
