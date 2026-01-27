//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

//
// WORK IN PROGRESS: This file is not yet fully implemented and may be subject to significant changes.
//

#pragma once

#include <system_error>

struct CapsuleOptions;
struct CommandDescriptor;

[[nodiscard]] std::error_code HandleResourceList(const CapsuleOptions& opts);
[[nodiscard]] std::error_code HandleResourceGet(const CapsuleOptions& opts);
[[nodiscard]] std::error_code HandleResourceSet(const CapsuleOptions& opts);
[[nodiscard]] std::error_code HandleResourceHexdump(const CapsuleOptions& opts);

/// @brief Return the descriptor that registers the 'resource' subcommand.
[[nodiscard]] CommandDescriptor GetResourceCommandDescriptor();
