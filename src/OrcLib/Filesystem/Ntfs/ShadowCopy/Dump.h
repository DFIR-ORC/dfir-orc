//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <string>

namespace Orc {
namespace Ntfs {
namespace ShadowCopy {

class Snapshot;
class SnapshotInformation;
class ShadowCopy;

void Dump(const std::string& path, const SnapshotInformation& info);
void Dump(const std::string& path, const Snapshot& snapshot);
void Dump(const std::string& path, const ShadowCopy& vss);

}  // namespace ShadowCopy
}  // namespace Ntfs
}  // namespace Orc
