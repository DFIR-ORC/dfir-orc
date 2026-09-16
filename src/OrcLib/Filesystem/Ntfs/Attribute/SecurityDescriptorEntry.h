//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <cstdint>

#include "Utils/BufferView.h"

namespace Orc {
namespace Ntfs {

// True when the $Secure:$SDS entry at 'offset' lies entirely inside 'sds', and so does the self-relative security
// descriptor it holds: its header, owner and group SIDs, and SACL/DACL with their ACEs. Only then may the entry be
// cast to SECURITY_DESCRIPTOR_ENTRY and its descriptor handed to the Windows security APIs, which trust every offset
// and length in it.
bool IsSecurityDescriptorEntryValid(BufferView sds, uint64_t offset);

}  // namespace Ntfs
}  // namespace Orc
