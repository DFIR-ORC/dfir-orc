//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#pragma once

#include "OrcLib.h"

#include <vector>

#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

namespace Config {

// Read a legacy top-level <upload> NodeList as a single required, unnamed destination. An entry that
// fails to configure is skipped while another remains, so one bad fallback does not cost the others;
// when none is left the error is returned and the run stops before collecting, because collecting
// with no upload leaves every archive on the host behind a single Error line.
HRESULT ReadLegacyUploadChain(const ConfigItem& uploadNode, std::vector<OutputSpec::UploadDestination>& destinations);

// Read a <uploads> block: each <destination> is one sequential destination, its <upload> children are
// its fallback chain. Filters inherit <uploads> -> <destination> -> <upload>, nearest wins, each
// attribute independently and keyed on attribute presence, so include="" means "deliberately empty,
// do not inherit". Refused (error returned): an empty block, a destination with no usable <upload>
// when it is required, a duplicate name (case-insensitively), and nothing left after the drops.
HRESULT ReadUploadDestinations(
    const ConfigItem& uploadsNode,
    std::vector<OutputSpec::UploadDestination>& destinations,
    bool& deleteWhenDone);

}  // namespace Config
}  // namespace Orc

#pragma managed(pop)
