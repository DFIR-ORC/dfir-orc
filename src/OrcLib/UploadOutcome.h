//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#pragma once

#include "OrcLib.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc {

// What one upload destination's chain came to, once its entries were exhausted.
struct UploadOutcome
{
    std::wstring Name;  // empty for a legacy top-level <upload> list
    bool Required = true;
    bool Succeeded = false;
    bool WantedNothing = false;  // its filters matched nothing, which is not a failure
};

// The severity a whole run's upload deserves, decided once after every destination has run.
enum class UploadVerdict
{
    AllSucceeded,  // Info
    OptionalFailed,  // Error: every required destination got the collection
    RequiredFailed,  // Critical: a destination the operator marked required received nothing
    LegacyChainFailed,  // Critical, in the exact wording a top-level <upload> list has always produced
};

// Grade the outcomes, and collect the names that received nothing so the caller logs once, at one
// level. A destination whose filters matched nothing is not a failure: a filter typo should be
// visible without condemning the run.
//
// One unnamed destination IS a legacy top-level <upload> list, and is graded separately so its total
// failure keeps producing today's message, which existing log parsers match on.
UploadVerdict GradeUploadOutcomes(
    const std::vector<UploadOutcome>& outcomes,
    std::vector<std::wstring>& failedRequired,
    std::vector<std::wstring>& failedOptional);

// How one chain entry is named in a log line: "Upload #2" for a legacy unnamed list, whose exact
// wording existing log parsers depend on; "Upload 'hq' #2/3" once destinations are named.
std::wstring UploadLabel(const std::wstring& destinationName, size_t entry, size_t chainSize);

// Whether this destination has any interest in one file, judged across its WHOLE fallback chain: each
// entry carries its own include/exclude, and a primary that filters a file out says nothing about a
// fallback that asks for everything. A run artifact ignores the filters entirely (it is the
// collection's own record); only artifacts="no" keeps one away.
bool DestinationWantsFile(
    const OutputSpec::UploadDestination& destination,
    const std::wstring& remoteName,
    bool isArtifact);

// Whether one local file may be deleted now that every destination has run (<uploads
// delete_when_done="yes">). True only when at least one REQUIRED destination wanted this file and
// every required destination that wanted it confirmed receipt.
//
// Every other shape fails closed, because this deletes evidence. The two that look true but are not:
// a configuration with no required destination at all, and a file no required destination wanted -
// "every required destination confirmed it" is vacuously true for both, and nothing was uploaded.
// A destination whose method cannot confirm per file (BITS) never appears in 'confirmedByDestination',
// so its files are kept by the same rule.
//
// 'isArtifact' marks one of the four files the collection writes about itself (outline, outcome, log,
// console). Those ignore include/exclude and reach every destination; only artifacts="no" keeps one
// away.
bool ShouldDeleteAfterUpload(
    const std::wstring& localPath,
    const std::wstring& remoteName,
    bool isArtifact,
    const std::vector<OutputSpec::UploadDestination>& destinations,
    const std::map<std::wstring, std::set<std::wstring>>& confirmedByDestination);

}  // namespace Orc

#pragma managed(pop)
